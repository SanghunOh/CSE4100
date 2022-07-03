#include "myshell.h"

// import environ
extern char** environ;
volatile sig_atomic_t sig_pid;
int bg;
int status;
char cmdline[MAXLINE];
int fd[2][2];

/* start signal handler */
void sigchld_handler(int sig)
{
	int en = errno;

	// wait for child process stopped or terminated
	while ((sig_pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
		if (WIFEXITED(status)) {	// exit normally
			int jobnum;
			if (jobnum = find_job(sig_pid)) {
				if (jobs[jobnum]->state == BACKGROUND)	// if terminated process is background process
					continue;	// wait for foreground process
			}
			process_done(sig_pid);
			break;
		}
		if (WIFSIGNALED(status)) {	// if terminated with signal
			if (WTERMSIG(status) == SIGINT)	// terminated with SIGINT
				Sio_puts("\n");	// change line

			int jobnum;
			if (jobnum = find_job(sig_pid)) {
				if (jobs[jobnum]->state == BACKGROUND)	// if terminated process is background process
					continue;
			}
			process_done(sig_pid);	// delete job from jobs list
			break;
		}
		if (WIFSTOPPED(status)) {	// if stopped
			int jobnum;
			Sio_puts("\n");	// change line
			if ((jobnum = find_job(sig_pid))) {	// find job, if job exists
				change_status(jobnum, STOPPED);	// change status to stopped
				print_one_job(*(jobs[jobnum]));	// print the job
			}
			else {
				add_jobs(sig_pid, STOPPED);	// if jobs does not exist
			}
			break;
		}
	}

	// restore errno
	errno = en;	
}

void sigint_handler(int sig) {
	// go to main (shell command)
	siglongjmp(env, 1);
}

/* end signal handler */ 

int main()
{
	// save stdin, stdout file descriptor
	stdin_fileno = dup(STDIN_FILENO);
	stdout_fileno = dup(STDOUT_FILENO);

	while (1)
	{
		// register sigint handler
		Signal(SIGINT, sigint_handler);
		if (sigsetjmp(env, 1) == 1)
			printf("\n");

		// prompt
		printf("CSE4100-SP-P#1> ");
		fflush(stdout);
		// read command
		Fgets(cmdline, MAXLINE, stdin);
		if (feof(stdin))
			exit(0);

		// execute command
		eval(cmdline);
	}
}

void eval(char* cmdline)
{
	int i;

	for(i = strlen(cmdline) - 2; i >= 0; i--){
		if (cmdline[i] == '&'){	// find if background command
			cmdline[i] = '\n';
			cmdline[i+1] = '\0';
			bg = 1;
			eval_with_pipe(cmdline);	// call eval with background flag

			return;
		}
		else if (cmdline[i] != ' ') {	// without background option
			bg = 0;
			eval_with_pipe(cmdline);	// call eval without background flag

			return;
		}
	}
}

void child_process(char *argv[MAXARGS], int nopipe, int pipe_idx, int fd[2][2], int last_process) {
	char execfile[50] = "/bin/";	// execfile location
	// allow sigint, sigtstp signals
	Signal(SIGINT, SIG_DFL);
	Signal(SIGTSTP, SIG_DFL);

	if (!nopipe) {	// without pipe
		if (!pipe_idx) {
			dup2(fd[1][0], STDIN_FILENO);
			if (!last_process) 
				dup2(fd[0][1], STDOUT_FILENO);
		}
		else {
			dup2(fd[0][0], STDIN_FILENO);
			if (!last_process)
				dup2(fd[1][1], STDOUT_FILENO);
		}
	}
	// close all fd
	close_all_fd(fd);

	// execute child process
	// commind in /bin/
	strcat(execfile, argv[0]);
	execve(execfile, argv, environ);

	// commind in /usr/bin
	strcpy(execfile, "/usr/bin/");
	strcat(execfile, argv[0]);
	execve(execfile, argv, environ);

	// child_exec failed

	// restore stdin, stdout if error occurs
	dup2(STDIN_FILENO, stdin_fileno);
	dup2(STDOUT_FILENO, stdout_fileno);

	//non-exist command
	printf("%s Command not found.\n", argv[0]);
	fflush(stdout);
	//exit child process

	exit(0);
}

void eval_with_pipe(char* cmdline)
{
	int nopipe = 1;	// indicate if it has no pipe
	char buf[MAXLINE] = "";
	char *argv[MAXARGS];	// arguments
	char *pipe_delim;	// parse by pipe
	char *temp_cmd;
	int pipe_idx = 0;	// 0, 1
	pid_t pid;
	sigset_t mask, prev;

	// sigset for block sigchild
	Sigemptyset(&mask);
	Sigaddset(&mask, SIGCHLD);

	// register sigchild handler
	Signal(SIGCHLD, sigchld_handler);

	temp_cmd = cmdline;

	// return if cmdline is blank
	if (!strcmp(temp_cmd, ""))
		return;
	
	// make fd[0] pipe
	pipe(fd[0]);
	pipe_idx = 1;
	while ((pipe_delim = strchr(temp_cmd, '|'))) {	// until there is no pipe '|'
		int len = pipe_delim - temp_cmd;	// length for one command
		strncpy(buf, temp_cmd, len);	// copy a command to buf
		buf[len++] = ' '; buf[len++] = '\0';

		nopipe = 0;

		pipe_delim++;
		temp_cmd = pipe_delim;

		parse_input(buf, argv);	// parse a commmand and store in argv

		if (!argv[0])	// return if blank
			return;

		if (pipe_idx == 0)	// make another pipe as per pipe_idx
			pipe(fd[0]);
		else
			pipe(fd[1]);
		if (!builtin_command(argv)) {	// if it is not built in command
			// block sigchild, sigint, sigtstp
			Sigprocmask(SIG_BLOCK, &mask, &prev);
			Signal(SIGINT, SIG_IGN);	// ignore SIGINT
			Signal(SIGTSTP, SIG_IGN);	// ignore SIGTSTP

			if ((pid = Fork()) == 0) {	// child process
				child_process(argv, nopipe, pipe_idx, fd, 0);
			}

			// parent process
			if (pipe_idx == 0){	// close useless file descriptos as per pipe_idx
				close(fd[1][0]);
				close(fd[1][1]);
			}
			else {
				close(fd[0][0]);
				close(fd[0][1]);
			}
			// set pipe_idx for next command
			pipe_idx = (pipe_idx + 1) % 2;
			
			// set sig_pid zero
			sig_pid = 0;
			if(!bg)	// if background option is activated
				while(!sig_pid)	// wait for sigchild signal
					Sigsuspend(&prev);	// sleep	

			// restore signal handlers and sigset
			Sigprocmask(SIG_SETMASK, &prev, NULL);
			signal(SIGINT, SIG_IGN);
			signal(SIGTSTP, SIG_IGN);
		}
	}

	// copy to buf for last command
	strcpy(buf, temp_cmd);
	// parse the last command
	parse_input(buf, argv);

	// return if blank
	if (!argv[0])
		return;

	// if there is no pipe, make fd[1] file descriptor
	if (nopipe)
		pipe(fd[1]);

	if (!builtin_command(argv)) { // if it is not build in command
		// block sigchild, sigint, sigtstp
		Sigprocmask(SIG_BLOCK, &mask, &prev);
		Signal(SIGINT, SIG_IGN);	// ignore SIGINT
		Signal(SIGTSTP, SIG_IGN);	// ignore SIGTSTP

		if ((pid = Fork()) == 0) {	// child process
			Signal(SIGINT, SIG_DFL);
			Signal(SIGTSTP, SIG_DFL);
			child_process(argv, nopipe, pipe_idx, fd, 0);
		}
		// parent process

		close_all_fd(fd);
		
		// set sig_pid zero
		sig_pid = 0;
		if (!bg)	// if background option is activated
			while(!sig_pid)	// wait for sigchild signal
				Sigsuspend(&prev);	// sleep

		if (bg) {
			add_jobs(pid, BACKGROUND);
		}

		// restore signal handlers and sigset
		Sigprocmask(SIG_SETMASK, &prev, NULL);
		signal(SIGINT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
	}

	// restore stdin, stdout file descriptors
	dup2(STDIN_FILENO, stdin_fileno);
	dup2(STDOUT_FILENO, stdout_fileno);

	return;
}

void close_all_fd(int fd[][2])
{
	// cloase all fd
	close(fd[0][0]);
	close(fd[0][1]);
	close(fd[1][0]);
	close(fd[1][1]);
}

int builtin_command(char **argv)
{
	if (!strcmp(argv[0], "exit")){	// input "exit"
		sigset_t mask;
		if (exit_all_process()) {	// if child process exists 
			sig_pid = 0;
			while(!sig_pid)	// wait for sigchild signal to reap child process
				Sigsuspend(&mask);	
		}
		exit(0);
	}
	if (!strcmp(argv[0], "&"))
		return 1;
	if (!strcmp(argv[0], "cd")){	// input "cd"
		Chdir(argv[1]);	// implepent change directory
		return 1;
	}
	if (!strcmp(argv[0], "jobs")) {	// if jobs
		print_jobs(0);	// print jobs
		return 1;
	}
	if (!strcmp(argv[0], "bg")) {	// if bg
		if (argv[1] && argv[1][0] == '%') {	// check input with jobs number
			int jobnum = atoi(argv[1] + 1) - 1;	// store job number

			if (!jobs[jobnum]) {	// if there is no job with the job number
				printf("No such job\n");
				
				return 1;
			}
			int pid = jobs[jobnum]->pid;	// get pid

			change_status(jobnum, BACKGROUND);	// change status to running
			print_one_job(*(jobs[jobnum]));	// print the jobs

			if (kill(pid, SIGCONT) < 0) {	// send SIGCONT
				unix_error("kill error");
			}
		}
		return 1;
	}
	if (!strcmp(argv[0], "fg")) {	// if fg
		if (argv[1] && argv[1][0] == '%') {	// check input with jobs number
			int jobnum = atoi(argv[1] + 1) - 1;	// store job number

			if (!jobs[jobnum]) {	// if there is no job with the job number
				printf("No such job\n");
				
				return 1;
			}
			sigset_t mask;
			int pid = jobs[jobnum]->pid;	// get pid
			strcpy(cmdline, jobs[jobnum]->command);
			struct termios stdinorg, stdinnew;
			struct termios stdoutorg, stdoutnew;
			Signal(SIGTTOU, SIG_IGN);	// ignore signals
			Signal(SIGTTIN, SIG_IGN);
			Signal(SIGTSTP, SIG_IGN);
			Signal(SIGINT, SIG_IGN);

			if (jobs[jobnum]->state == STOPPED)	// if jobs is stopped
				kill(pid, SIGCONT);		// send SIGCONT

			change_status(jobnum, FOREGROUND);

			tcgetattr(fileno(stdin), &stdinorg);	// get terminal attr
			tcgetattr(fileno(stdout), &stdoutorg);	// get terminal attr

			stdinnew = stdinorg;
			stdoutnew = stdoutorg;

			tcsetattr(fileno(stdout), TCSADRAIN, &stdoutnew);	// set terminal attr
			tcsetattr(fileno(stdin), TCSADRAIN, &stdinnew);	// set terminal attr

			tcsetpgrp(fileno(stdout), getpgid(getpid()));	// change terminal controlling process
			tcsetpgrp(fileno(stdin), getpgid(getpid()));	// change terminal controlling process
			if (kill (pid, SIGCONT) < 0){	// send SIGCONT
				unix_error("kill error");
			}
			//kill sigcont all

			sig_pid = 0;
			while(!sig_pid)	// wait for sigchild signal
				Sigsuspend(&mask);	// sleep

			Signal(SIGTTOU, SIG_DFL);
			Signal(SIGTTIN, SIG_DFL);
			Signal(SIGTSTP, SIG_DFL);
			Signal(SIGTSTP, SIG_DFL);
		}
		
		return 1;
	}
	if (!strcmp(argv[0], "kill")) {	// if kill
		if (argv[1] && argv[1][0] == '%') {	// check input with job number
			int jobnum = atoi(argv[1] + 1) - 1;	// get job number
			if (!jobs[jobnum]) {	// if there is no job with the job number
				printf("No such job\n");
				
				return 1;
			}
			int pid = jobs[jobnum]->pid;	// get pid

			kill(pid, SIGKILL);	// send SIGKILL
		}

		return 1;
	}

	return 0;
}

void Chdir(char* argv)
{
	struct stat file_info;
	

	if(argv == '\0' || !strcmp(argv, "~") || !strcmp(argv, "~/")) { // change dir to home dir
		chdir(getenv("HOME"));

		return;
	}

	if (stat(argv, &file_info)) {	// get file_info
		if (access(argv, F_OK)) {	// if dir does not exist
			printf("-bash: cd: %s: No such file or directory\n", argv);

		return;
		}
	}

	if ((file_info.st_mode & S_IFMT) != S_IFDIR) {	// check if directory
		printf("-bash: cd: %s: Not a directory\n", argv);

		return;
	}	

	if (access(argv, R_OK | W_OK)) {	// if dir is readable or writable
		printf("-bash: cd: %s: Permission denied\n", argv);

		return;
	}

	if (chdir(argv) == -1) {	// change directory
		unix_error("Change directory error");
	}

	return;
}

void process_done(pid_t pid) {
	// when process is finished
	for (int i = 0; i < jobs_max; i++) {
		if (jobs[i] == NULL)
			continue;
		if (jobs[i]->pid == pid) {	// find jobs with pid
			jobs_cnt--;	// reduce jobs count by 1
			if (jobs_cnt == 0)	// if jobs count is 0
				jobs_max = 0;	// max job num is 0

			free(jobs[i]);	// free memory
			jobs[i] = NULL;	// avoid dangling pointer

			break;
		}
	}
}

void change_status(int jobnum, int opt) {
	// change jobs status to opt
	if (!jobs[jobnum]){
		Sio_puts("jobs not found\n");
		return;
	}
	jobs[jobnum]->state = opt;
}

int exit_all_process() {
	int child = 0;
	for (int i = 0; i < jobs_max; i++) {
		if (jobs[i] == NULL)
			continue;
		// send SIGKILL to all jobs
		kill(jobs[i]->pid, SIGKILL);
		child = 1;
	}

	return child;
}

int find_job(pid_t pid) {
	// find a job with the pid
	int ret = 0;
	for (int i = 0; i < jobs_max; i++) {
		if (jobs[i] == NULL)
			continue;

		if (jobs[i]->pid == pid){
			ret = jobs[i]->num;
		}
	}
	return ret;
}

void add_jobs(pid_t pid, int status) {
	// append new job
	BGProcess *n = (BGProcess*)malloc(sizeof(BGProcess));
	// store data
	n->num = jobs_max++;
	n->pid = pid;
	strcpy(n->command, cmdline);
	n->state = status;
	
	jobs[jobs_max - 1] = n;
	jobs_cnt++;

	Sio_puts("[");
	Sio_putl(n->num + 1);
	if (n->state == BACKGROUND) { // if background process
		Sio_puts("] ");
		Sio_putl(pid);	// print pid only
		Sio_puts("\n");
	}
	else if (n->state == STOPPED) {	// if job stopped
		Sio_puts("]\tstopped\t\t");	// print inform
		Sio_puts(n->command);
	}

	return;
}

void print_one_job(BGProcess job) {
	// print one jobs
	Sio_puts("[");
	Sio_putl(job.num + 1);
	if (job.state == BACKGROUND | job.state == FOREGROUND) // bg, fg process
		Sio_puts("]\trunning\t\t");
	else	// stopped process
		Sio_puts("]\tstopped\t\t");	
	Sio_puts(job.command);
}

void print_jobs(char option) {
	// print all jobs
	for (int i = 0; i < jobs_max; i++) {
		if (jobs[i] == NULL)
			continue;
		print_one_job(*(jobs[i]));
	}
	return;
}


int parse_input(char* buf, char **argv) 
{
	// parse one command
	char* delim_pointer;
	int argc = 0;
	char* single_quote = 0;
	char* double_quote = 0;

	buf[strlen(buf) - 1] = ' ';

	// trim front
	while (*buf && (*buf == ' '))
		buf++;

	while ((delim_pointer = strchr(buf, ' '))) {	// parse word
		single_quote = strchr(buf, '\'');
		double_quote = strchr(buf, '\"');
		if ((single_quote && single_quote < delim_pointer) || \
				(double_quote && double_quote < delim_pointer)) {	
			if (single_quote || double_quote) {
				buf = single_quote ? single_quote : double_quote;
				buf++;
				delim_pointer = single_quote ? strchr(buf, '\'') : strchr(buf, '\"');
			}
		}
		argv[argc++] = buf;
		*delim_pointer = '\0';
		buf = delim_pointer + 1;
		while (*buf && (*buf == ' '))	// remove blank
			buf++;
	}

	argv[argc] = NULL;	// indicate finish

	if (!argc)	// input blank
		return 1;

	return 0;
}

/* wrapper function */
pid_t Fork() {
	pid_t pid;

	if((pid = fork()) < 0)
		unix_error("Fork error");

	return pid;
}

char* Fgets(char* ptr, int n, FILE *stream)
{
	char* rptr;

	if (((rptr = fgets(ptr, n, stream)) == NULL) && ferror(stream)){
		app_error("Fgets error");
	}

	return rptr;
}

pid_t Waitpid(pid_t pid, int *iptr, int options)
{
	pid_t retpid;

	if ((retpid = waitpid(pid, iptr, options)) < 0)
		unix_error("Waitpid error");
	return retpid;
}

void Kill(pid_t pid, int signum)
{
	int rc;

	if ((rc = kill(pid, signum)) < 0)
		unix_error("Kill error");
}

handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* Block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* Restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
		unix_error("Signal error");
    return (old_action.sa_handler);
}

void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
	if (sigprocmask(how, set, oldset) < 0)
		unix_error("Sigprocmask error");
	return;
}

void Sigemptyset(sigset_t *set)
{
	if (sigemptyset(set) < 0)
		unix_error("Sigemptyset error");
	return;
}

void Sigfillset(sigset_t *set)
{
	if (sigfillset(set) < 0)
		unix_error("Sigaddset error");
	return;
}

void Sigaddset(sigset_t *set, int signum)
{
	if (sigaddset(set, signum) < 0)
		unix_error("Sigaddset error");
	return;
}

void Sigdelset(sigset_t *set, int signum)
{
	if (sigdelset(set, signum) < 0)
		unix_error("Sigdelset error");
	return;
}

int Sigsuspend(const sigset_t *set)
{
	int rc = sigsuspend(set);
	if (errno != EINTR)
		unix_error("Sigsuspend error");
	return rc;
}

/* end signal wrapper */

/* Private sio functions */

/* $begin sioprivate */
/* sio_reverse - Reverse a string (from K&R) */
static void sio_reverse(char s[])
{
    int c, i, j;

    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* sio_ltoa - Convert long to base b string (from K&R) */
static void sio_ltoa(long v, char s[], int b) 
{
    int c, i = 0;
    
    do {  
        s[i++] = ((c = (v % b)) < 10)  ?  c + '0' : c - 10 + 'a';
    } while ((v /= b) > 0);
    s[i] = '\0';
    sio_reverse(s);
}

/* sio_strlen - Return length of string (from K&R) */
static size_t sio_strlen(char s[])
{
    int i = 0;

    while (s[i] != '\0')
        ++i;
    return i;
}
/* $end sioprivate */

/* Public Sio functions */
/* $begin siopublic */

ssize_t sio_puts(char s[]) /* Put string */
{
    return write(STDOUT_FILENO, s, sio_strlen(s)); //line:csapp:siostrlen
}

ssize_t sio_putl(long v) /* Put long */
{
    char s[128];
    
    sio_ltoa(v, s, 10); /* Based on K&R itoa() */  //line:csapp:sioltoa
    return sio_puts(s);
}

void sio_error(char s[]) /* Put error message and exit */
{
    sio_puts(s);
    _exit(1);                                      //line:csapp:sioexit
}
/* $end siopublic */

/*******************************
 * Wrappers for the SIO routines
 ******************************/
ssize_t Sio_putl(long v)
{
    ssize_t n;
  
    if ((n = sio_putl(v)) < 0)
	sio_error("Sio_putl error");
    return n;
}

ssize_t Sio_puts(char s[])
{
    ssize_t n;
  
    if ((n = sio_puts(s)) < 0)
	sio_error("Sio_puts error");
    return n;
}

void Sio_error(char s[])
{
    sio_error(s);
}

/* Unix I/O wrapper */

void Close(int fd)
{
	if (close(fd) < 0)
		unix_error("Close error");
}


/* Error functions */
void unix_error(char* msg)
{
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));

	exit(0);
}

void app_error(char* msg)
{
	fprintf(stderr, "%s\n", msg);

	exit(0);
}

