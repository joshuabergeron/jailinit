#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/signal.h>
#include <errno.h>
#include <sys/reboot.h>
#include <paths.h>

/* system startup and shutdown scripts */
#define _PATH_RUNCOM "/etc/rc"
#define _PATH_RUNDOWN "/etc/rc.shutdown"

typedef long (*state_func_t) __P((void));
typedef state_func_t (*state_t) __P((void));

state_func_t multi_user __P((void));
state_func_t single_user __P((void));
state_func_t death_user __P((void));
state_func_t restart_user __P((void));
state_func_t idle_user __P((void));
 
static void transition_handler(int sig);
static void transition(state_t s);

state_t requested_transition;
state_t curstate;

int main(int argc, char *argv[])
{
	int c;
	int ret;
	pid_t pid;

	if (getuid() != 0) {
		return -1;
	}
	
	if (getpid() == 1) {
		return -1;
	}

	/*
	 * maybe sysv init style stuff later on.
	 */
	while ((c = getopt(argc, argv, "ms")) != -1)
		switch(c) {
		case 'm':
			requested_transition = multi_user;
			break;
		case 's':
			requested_transition = single_user;
		default:
			printf("no such flag: -%c", c);
			break;
		}	
	
	if (optind != argc)
		printf("ignoring excess arguments");

	if ((pid = fork()) == 0) {	
	
		setproctitle("vm1");

	/*
	 * there are a few signals we will want to catch
	 * and deal with.  we use SIGHUP for restarting the jail.
	 * later on it may be better to use sigset function 
	 */

		signal(SIGSYS, SIG_IGN);
		signal(SIGABRT, SIG_IGN);
		signal(SIGILL, SIG_IGN);
		signal(SIGSEGV, SIG_IGN);
		signal(SIGKILL, SIG_IGN);
		signal(SIGHUP, transition_handler);
		signal(SIGINT, SIG_IGN);
		signal(SIGUSR1, SIG_IGN);
		signal(SIGUSR2, SIG_IGN);
		signal(SIGFPE,	SIG_IGN);
		signal(SIGXFSZ, SIG_IGN);
		signal(SIGTERM, SIG_IGN);
		signal(SIGXCPU, SIG_IGN);
		signal(SIGBUS, SIG_IGN);
		signal(SIGALRM, SIG_IGN);
	
		/* actually start the machine. */
		transition(multi_user);

		for (;;) {
			sleep(5);
		}
	
		/*
	 	 * should never get here, obviously. (with any luck)
	 	 */
		return -1;
	}
}

static void transition_handler(int sig)
{
	int status;

	switch(sig) {
	case SIGHUP:
		/* re-establish signal handler */
		// funny story about this. ask me some time.
		signal(SIGHUP, transition_handler);
		transition(restart_user);
		break;
	case SIGTERM:
		signal(SIGTERM, transition_handler);
		transition(idle_user);
		break;
	}
	return;
}

/*
 * move to a new state
 */
static void transition(state_t s)
{
	//signal(SIGHUP, transition_handler);

	if (curstate != s) { 
		curstate = s;
		s = (state_t) (*s)();
	}
	return;
}

state_func_t idle_user()
{
	static const int death_digs[2] = { SIGTERM, SIGKILL};
	int i;

	for (i = 0; i < 2; i++) {
		if (kill(-1, death_digs[i]) == -1 && errno == ESRCH)
			return (state_func_t) idle_user;
	}
	return (state_func_t) idle_user;
}

/*
 * kill all processes in the jail 
 */
state_func_t death_user()
{
	pid_t pid;
	int i;
	static const int death_sigs[2] = { SIGTERM, SIGKILL };

	for (i = 0; i < 2; i++) {
		if (kill(-1, death_sigs[i]) == -1 && errno == ESRCH)
			return (state_func_t) single_user; 
	}
	return (state_func_t) single_user;
}

state_func_t restart_user()
{
	pid_t pid;
	int i;
	static const int death_sigs[2] = { SIGTERM, SIGKILL };

    // TODO
	// for some reason the handler isnt getting established 
	// after an initial hup, inside transition_handler() ?
	// signal(SIGHUP, transition_handler);

	printf("restarting processes");	
	for (i = 0; i < 2; i++) {
		if (kill(-1, death_sigs[i]) == -1 && errno == ESRCH)
			return (state_func_t) single_user;
	}
	transition(multi_user);
}

state_func_t single_user()
{
	pid_t pid;
	
	/* do securelevel stuff here. */

	/* TODO maybe a single user mode - not needed currently though */

	transition(multi_user);	
}

/*
 * multi user state:
 * this will involve executing RUNCOM script. generally /etc/rc.
 */

state_func_t multi_user()
{
	pid_t pid;
	char *argv[3];
	int rv;

	if ((pid = fork()) == 0) {
		argv[0]	= "sh";
		argv[1] 	= _PATH_RUNCOM;
		argv[2]	= 0;	

		execv(_PATH_BSHELL, argv);
		rv = 0;
		exit(rv);
	}
	wait(&rv);
	return;
}
