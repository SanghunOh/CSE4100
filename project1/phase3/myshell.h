/*
	project: SP project#1 phase3: Run processing in Background in Shell
	studentID: 20181654
	name: 오상훈
	copyrightⓒ 2022 All rights reserved by 오상훈
*/
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>

#define MAXLINE 8192
#define MAXARGS 128

#define BACKGROUND 0
#define FOREGROUND 1
#define STOPPED 2

typedef void handler_t(int);

typedef struct {
	int num;
	pid_t pid;
	pid_t pgid;
	char command[128];
	char state;
} BGProcess;

BGProcess *jobs[MAXARGS];
int jobs_cnt;
int jobs_max;
int stdin_fileno;
int stdout_fileno;
static sigjmp_buf env;


void eval(char*);
void eval_with_pipe(char*);
int parse_input(char*, char**);
void child_process(char *argv[MAXARGS], int nopipe, int pipe_idx, int fd[2][2], int last_process);
void child_exec(char *, char *[MAXARGS]);
int builtin_command(char **);
void close_all_fd(int [][2]);

// process cd command
void Chdir(char*);

void add_jobs(int, int);
void print_jobs(char option);
void print_one_job(BGProcess);
void process_done(int pid);
void change_status(int, int);
int find_job(pid_t);
int exit_all_process();

// wrapper function
pid_t Fork();
char *Fgets(char*, int, FILE*);
pid_t Waitpid(pid_t, int *, int);
void Kill(pid_t, int);
handler_t *Signal(int signum, handler_t *handler);
void Sigprocmask(int, const sigset_t *, sigset_t *);
void Sigemptyset(sigset_t *);
void Sigaddset(sigset_t *, int);
void Sigfillset(sigset_t *);
void Sigdelset(sigset_t *, int);
int Sigsuspend(const sigset_t *);

/* Sio (Signal-safe I/O) routines */
ssize_t sio_puts(char s[]);
ssize_t sio_putl(long v);
void sio_error(char s[]);

/* Sio wrappers */
ssize_t Sio_puts(char s[]);
ssize_t Sio_putl(long v);
void Sio_error(char s[]);

/* Unix I/O wrapper */
void Close(int);

// error function
void unix_error(char* msg);
void app_error(char* msg);

