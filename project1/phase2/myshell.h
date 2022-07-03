/*
	project: SP project#1 phase2: Redirection and Piping in Shell
	studentID: 20181654
	name: 오상훈
	copyrightⓒ 2022 All rights reserved by 오상훈
*/
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

typedef void handler_t(int);

static sigjmp_buf env;

void eval(char*);
int parse_input(char*, char**);
void eval_with_pipe(char*);
void child_exec(char *, char *[MAXARGS]);
int builtin_command(char **);
void close_all_fd(int [][2]);

// process cd command
void Chdir(char*);

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

