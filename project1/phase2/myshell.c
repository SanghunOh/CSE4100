#include "myshell.h"

// import environ
extern char** environ;
volatile sig_atomic_t sig_pid;
char cmdline[MAXLINE];

/* start signal handler */
void sigchld_handler(int sig)
{
	int en = errno;
	int status;

	// wait for child process stopped or terminated
	while ((sig_pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
		if (WIFEXITED(status)) {	// exit normally
			break;
		}
		if (WIFSIGNALED(status)) {	// if terminated with signal
			if (WTERMSIG(status) == SIGINT)	// terminated with SIGINT
				Sio_puts("\n");	// change line
			break;
		}

		if (WIFSTOPPED(status)) {	// if stopped
			Sio_puts("\n");
			break;
		}
	}

	errno = en;	
}

void sigint_handler(int sig) {
	siglongjmp(env, 1);
}

/* end signal handler */ 

int main()
{

	int i;
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
		eval_with_pipe(cmdline);
	}
}


void eval_with_pipe(char* cmdline)
{
	int nopipe = 1;	// indicate if it has no pipe
	int fd[2][2];
	int tempin, tempout;
	char buf[MAXLINE] = "";	// arguments
	char *argv[MAXARGS];
	char *pipe_delim;
	char *temp_cmd;
	int pipe_idx = 0;
	pid_t pid;
	int status;
	char execfile[50] = "/bin/";	// execfile location
	sigset_t mask, prev;

	// sigset for block sigchld
	Sigemptyset(&mask);
	Sigaddset(&mask, SIGCHLD);

	// register sigchild handler
	Signal(SIGCHLD, sigchld_handler);

	temp_cmd = cmdline;

	// return if cmdline is blank
	if (!strcmp(temp_cmd, ""))
		return;
	
	tempin = dup(STDIN_FILENO);
	tempout = dup(STDOUT_FILENO);

	// make fd[0] pipe
	if(pipe_idx == 0)
		pipe(fd[0]);
	else
		pipe(fd[1]);
	pipe_idx = 1;
	while ((pipe_delim = strchr(temp_cmd, '|'))) { // until there is no pipe
		int len = pipe_delim - temp_cmd;	// length for one command
		strncpy(buf, temp_cmd, len);	// copy a command to buf
		buf[len++] = ' '; buf[len++] = '\0';

		nopipe = 0;

		pipe_delim++;
		temp_cmd = pipe_delim;

		parse_input(buf, argv);	// parse a command and store in argv

		if (!argv[0])	// return if blank
			return;

		if (pipe_idx == 0)	// make another pipe as per pipe_idx
			pipe(fd[0]);
		else
			pipe(fd[1]);
		if (!builtin_command(argv)) {	// if it is not built in command
			// block sigchild, sigint, sigtstp
			Sigprocmask(SIG_BLOCK, &mask, &prev);
			Signal(SIGINT, SIG_IGN);
			Signal(SIGTSTP, SIG_IGN);

			if ((pid = Fork()) == 0) {	// child process
				Signal(SIGINT, SIG_DFL);
				Signal(SIGTSTP, SIG_DFL);

				if (pipe_idx == 0) {	// connect pipe
					dup2(fd[0][1], STDOUT_FILENO);
					dup2(fd[1][0], STDIN_FILENO);
				}
				else {	// connect pipe
					dup2(fd[0][0], STDIN_FILENO);
					dup2(fd[1][1], STDOUT_FILENO);
				}
				// close all fd
				close_all_fd(fd);
				
				//execute child process
				child_exec(execfile, argv);

				// child_exec failed
				// resotre stdin, stdout if error occurs
				dup2(STDIN_FILENO, tempin);
				dup2(STDOUT_FILENO, tempout);
	
				// non-exist command
				printf("%s: Command not found.\n", argv[0]);
				fflush(stdout);
				//exit child process
				exit(0);
			}
			// parent process
			if (pipe_idx == 0){	// close uesless file descriptors as per pipe_index
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
			while(!sig_pid){	// wait for sigchld signal
				Sigsuspend(&prev);	// sleep
			}

			// restore signal handlers and sigset
			Sigprocmask(SIG_SETMASK, &prev, NULL);
			signal(SIGINT, SIG_DFL);
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

	if (!builtin_command(argv)) {	// if it is not built in command
		// block sigchild, sigint, sigtstp
		Sigprocmask(SIG_BLOCK, &mask, &prev);
		Signal(SIGINT, SIG_IGN);
		Signal(SIGTSTP, SIG_IGN);

		if ((pid = Fork()) == 0) {	// child process
			Signal(SIGINT, SIG_DFL);
			Signal(SIGTSTP, SIG_DFL);

			if(!nopipe) {	// without pipe
				if (pipe_idx == 0) {	// redirection to stdin as per pipe_idx
					dup2(fd[1][0], STDIN_FILENO);
				}
				else {
					dup2(fd[0][0], STDIN_FILENO);
				}
			}
			close_all_fd(fd);
			
			// execute child process
			child_exec(execfile, argv);

			//child_exec failed
			
			// restore stdin, stdout if error occurs
			dup2(STDIN_FILENO, tempin);
			dup2(STDOUT_FILENO, tempout);

			// non-exist command
			printf("%s: Command not found.\n", argv[0]);
			fflush(stdout);
			//exit child process
			exit(0);
		}
		// parent process
		close_all_fd(fd);

		// set sig_pid zero
		sig_pid = 0;
		while(!sig_pid)	// wait for sigchild signal
			Sigsuspend(&prev);	// sleep

		// restore signal handlers and sigset
		Sigprocmask(SIG_SETMASK, &prev, NULL);
		signal(SIGINT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
	}

	// restore stdin, stdout file descriptors
	dup2(STDIN_FILENO, tempin);
	dup2(STDOUT_FILENO, tempout);

	return;
}

void child_exec(char *execfile, char *argv[MAXARGS])
{
	// commind in /bin/
	strcat(execfile, argv[0]);
	execve(execfile, argv, environ);

	// commind in /usr/bin
	strcpy(execfile, "/usr/bin/");
	strcat(execfile, argv[0]);
	execve(execfile, argv, environ);

	return;
}

void close_all_fd(int fd[][2])
{
	// close all fd
	close(fd[0][0]);
	close(fd[0][1]);
	close(fd[1][0]);
	close(fd[1][1]);
}

int builtin_command(char **argv)
{
	if (!strcmp(argv[0], "exit"))	// input "exit"
		exit(0);
	if (!strcmp(argv[0], "&"))
		return 1;
	if (!strcmp(argv[0], "cd")){	// input "cd"
		Chdir(argv[1]);	// implement change directory

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

int parse_input(char* buf, char **argv) 
{
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

	if (((rptr = fgets(ptr, n, stream)) == NULL) && ferror(stream))
		app_error("Fgets error");

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

