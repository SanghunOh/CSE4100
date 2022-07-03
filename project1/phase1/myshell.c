#include "myshell.h"

// import environ
extern char** environ;
volatile sig_atomic_t sig_pid;

// sigchild handler
void sigchld_handler(int sig)
{
	// savve errno
	int en = errno;
	int status;
	fflush(stdout);

	// wait for child process terminated or stopped
	while ((sig_pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
		if (WIFEXITED(status)) {	// exit normally
			break;	
		}
		if (WIFSIGNALED(status)) {	// if terminated with signal
			if (WTERMSIG(status) == SIGINT){	// terminated with SIGINT
				Sio_puts("\n");	// change line
				break;
			}
		}
		if (WIFSTOPPED(status)) {	// if stopped 
			Sio_puts("\n");
			break;
		}
	}

	fflush(stdout);

	// restore errno
	errno = en;
	
	return;
}

void sigint_handler(int sig) {
	// go to shell command
	siglongjmp(env, 1);
}

/* end signal handler  */

int main()
{
	char cmdline[MAXLINE];

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
		eval(cmdline);
	}
}

void eval(char* cmdline)
{
	char *argv[MAXARGS];
	char buf[MAXLINE];
	char execfile[50] = "/bin/";
	pid_t pid;
	sigset_t mask, prev;

	// sigset for block sigchld
	Sigemptyset(&mask);
	Sigaddset(&mask, SIGCHLD);
	Signal(SIGCHLD, sigchld_handler);

	// copy to buf and parse command
	strcpy(buf, cmdline);
	parse_input(buf, argv);

	if (!argv[0])	// input none(blank)
		return;

	if (!strcmp(argv[0], "clear")) {	// input "clear"
		system("clear");

		return;
	}

	if (!builtin_command(argv)) {
		Sigprocmask(SIG_BLOCK, &mask, &prev);
		signal(SIGINT, SIG_IGN);	// ignore SIGINT
		signal(SIGTSTP, SIG_IGN);	// ignore SIGTSTP

		if ((pid = Fork()) == 0) {	// child process
			signal(SIGINT, SIG_DFL);	// allow SIGINT
			signal(SIGTSTP, SIG_DFL);	// allow SIGTSTP
			int status = 0;	

			// commind in /bin/
			strcat(execfile, argv[0]);
			status = execve(execfile, argv, environ);
			
			// commind in /usr/bin
			strcpy(execfile, "/usr/bin/");
			strcat(execfile, argv[0]);
			status = execve(execfile, argv, environ);

			// non-exist command
			printf("%s: Command not found.\n", argv[0]);
			fflush(stdout);
			//exit child process
			exit(0);
		}
		// parent process
		sig_pid = 0;
		while(!sig_pid){	// wait for child process done
			Sigsuspend(&prev);
		}

		//restore signal handlers and sigset
		Sigprocmask(SIG_SETMASK, &prev, NULL);
		signal(SIGINT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
	}

	return;
}

int builtin_command(char **argv)
{
	if (!strcmp(argv[0], "exit"))	// input "exit"
		exit(0);
	if (!strcmp(argv[0], "&"))
		return 1;
	if (!strcmp(argv[0], "cd")){	// input "cd"
		Chdir(argv[1]);

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
		single_quote = strchr(buf, '\'');	// find single quote
		double_quote = strchr(buf, '\"');	// find double quote
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

