
/* CMD.C is a program to execute a command sending input lines to it.
   In this case it will change password for a Kerberos principal. */

/* System include files. */
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

/* Status codes. */
#define SUCCESS ((int) 0)
#define FAILURE ((int) 1)

/* Truth values. */
#define true ((int) 1)
#define false ((int) 0)

/* Sides of the pipe. */
#define READ_END 0
#define WRITE_END 1

/* End of string character. */
#define EOS '\0'

/* New line string. */
#define NEWLINESTRING "\n"

/* Line length. */
#define LINELENGTH 2048

/* Kerberos limitations. I was unable to find a definitive answer
   on these. These are conservative values. */

/* Kerberos principal and realm name length limit.
   There is conflicting information to be found on this. */
/* #define KRB_LIMIT 240 */
#define KRB_LIMIT 512

/* Password length limit. */
#define PASSWORD_LIMIT 512

/* Allowed characters in username / principal. */
#define USERCHARS "@_.-/"

/* Allowed characters in password. */
#define PASSCHARS "~@*_.-+:?/{}[]"

/* Report error and quit. */

static void
error (char *format, ...)
{
	va_list args;

	va_start (args, format);
	(void) vfprintf (stderr, format, args);
	(void) fprintf (stderr, "\n");
	(void) fflush (stderr);
	va_end (args);
	exit (FAILURE);
}

/* Print message on stdout. */

static void
msg (char *format, ...)
{
	va_list args;

	/* Print message on stdout with new line. */
	va_start (args, format);
	(void) vfprintf (stdout, format, args);
	(void) fprintf (stdout, "\n");
	(void) fflush (stdout);
	va_end (args);
}

/* Zero string and free. */

static void
zerofree (char *s)
{
	size_t len;

	len = strlen (s);
	(void) memset (s, 0, len);
	free (s);
}

/* Control character. */

static int
iscontrol (char c)
{
	if (c < 0x20 || c > 0x7e)
	{
		return (true);
	}
	return (false);
}

/* Check for alien characters. */

static int
aliens (char *s, char *a)
{
	int ls;
	int la;
	int i;
	int j;
	char chs;
	char cha;
	int found;

	ls = strlen (s);
	la = strlen (a);
	for (i=0; i<ls; i++)
	{
		chs = s[i];
		if (! isalnum (chs))
		{

			/* Not letter or number. */
			if (iscontrol (chs))
			{

				/* Control character, alien. */
				return (true);
			}

			/* Check for other allowed characters. */
			found = false;
			for (j=0; j<la; j++)
			{
				cha = a[j];
				if (chs == cha)
				{

					/* Found, it's legal. */
					found = true;
				}
			}
			if (! found)
			{

				/* Not on the allowed list. */
				return (true);
			}
		}
	}

	/* We are OK. */
	return (false);
}

/* Default signal handler. */

static void
sighand (int signo)
{
	if (signo == SIGHUP || signo == SIGINT ||
		signo == SIGQUIT || signo == SIGTERM)
	{
		exit (FAILURE);
	}
	else
	{
		error ("Interrupted, signal %d - confused", signo);
	}
}

/* Signal handler for SIGCHLD. */

static void
handchld (int signo)
{
	return;
}

/* Signal handler for SIGPIPE. */

static void
handpipe (int signo)
{
	return;
}

/* Declare signal handler. */

static void
sig (int s, void (*handler)(int))
{

	/* Signal action block to sigaction. */
	struct sigaction sablock;
	struct sigaction *sa;

	/* Status code. */
	int status;

	sa = &sablock;
	(void) memset (sa, 0, sizeof (struct sigaction));
	sa->sa_handler = handler;
	status = sigemptyset (&(sa->sa_mask));
	if (status == -1)
	{
		error ("Error calling sigemptyset - confused");
	}
	sa->sa_flags = 0;
	status = sigaction (s, sa, NULL);
	if (status == -1)
	{
		error ("Error %d calling sigaction - confused", errno);
	}
}

/* Execute command and send lines. */

static int
cmd (char *file, char *args[], char *env[], char *lines[])
{

	/* Return code from system library call. */
	int status;

	/* Child PID. */
	pid_t pid;

	/* Pipe we use for the child. */
	int pipefd[2];

	/* PID returned by waitpid. */
	pid_t wpid;

	/* Status codes. */
	int wstatus;
	int estatus;

	/* Sending lines to the execed process. */
	int i;
	char *line;
	size_t linelength;
	ssize_t writestatus;

	/* Saved STDOUT. */
	int stdout2;

	/* Create pipe. */
	status = pipe (pipefd);
	if (status == -1)
	{
		error ("Error %d creating pipe", errno);
	}

	/* Fork. */
	pid = fork ();
	if (pid < (pid_t) 0)
	{
		error ("Error %d forking", errno);
	}
	else if (pid == (pid_t) 0)
	{

		/* Child. */

		/* Connect STDIN to input side of pipe and close write end
		   since that is not needed and the read end has a copy as fd 0. */
		status = dup2 (pipefd[READ_END], STDIN_FILENO);
		if (status == -1)
		{
			error ("Error %d calling dup2 in child", errno);
		}
		status = close (pipefd[WRITE_END]);
		if (status != 0)
		{
			error ("Error %d closing write end of pipe in child", errno);
		}
		status = close (pipefd[READ_END]);
		if (status != 0)
		{
			error ("Error %d closing read end of pipe in child", errno);
		}

		/* Run command. */
		estatus = execve (file, args, env);
		if (estatus == -1)
		{
			error ("Execve failed with %d", errno);
		}

		/* Would not reach here. */
		exit (SUCCESS);
	}

	/* Parent. */

	/* Dup STDOUT. Save first since we will close it. */
	stdout2 = dup (STDOUT_FILENO);
	status = dup2 (pipefd[WRITE_END], STDOUT_FILENO);
	if (status == -1)
	{
		error ("Error %d calling dup2 in child", errno);
	}

	/* Close read end in parent since not needed. */
	status = close (pipefd[READ_END]);
	if (status != 0)
	{
		error ("Error %d closing read end of pipe in parent", errno);
	}

	/* Close write end since we got it as STDOUT. */
	status = close (pipefd[WRITE_END]);
	if (status != 0)
	{
		error ("Error %d closing read end of pipe in parent", errno);
	}

	/* Send lines. */
	if (lines == NULL)
	{
		error ("No lines");
	}
	i = 0;
	line = lines[i];
	while (line != NULL)
	{
		linelength = strlen (line);
		writestatus = write (STDOUT_FILENO, line, linelength);
		if (writestatus == -1)
		{
			error ("Error %d writing in cmd", errno);
		}
		if (writestatus != (ssize_t) linelength)
		{
			error ("Short write in cmd - confused");
		}
		i++;
		line = lines[i];
	}

	/* Close so the forked process can finish.
	   Note that there will be not output after this. */
	status = close (STDOUT_FILENO);
	if (status == -1)
	{
		error ("Error %d closing output in cmd", errno);
	}

	/* Restore. */
	status = dup2 (stdout2, STDOUT_FILENO);
	if (status == -1)
	{
		error ("Error %d restoring output in cmd", errno);
	}

	/* Wait for child to terminate. */
	wpid = waitpid (pid, &wstatus, 0);
	if (wpid == (pid_t) -1)
	{
		error ("Error %d returned by waitpid", errno);
	}
	if (WIFEXITED (wstatus))
	{

		/* OK. */
		;
	}
	else if (WIFSIGNALED (wstatus))
	{
		error ("Child process %d exited on %d", (int) wpid, WTERMSIG(wstatus));
	}
	else
	{
		error ("Weird exit from waitpid - confused");
	}

	/* Get status. */
	status = WEXITSTATUS (wstatus);

	/* Return with command status. */
	return (status);
}

/* Change password. */

static int
cpw (char *user, char *password)
{

	/* Command and arguments. */
/* !!!!
	char *c = "/bin/kadmin";
	char *a[] =
	{
		"kadmin",
		"-p",
		"ipaadmin/changepw@SWESTORE.SE",
		"-k",
		"-t",
		"/home/ipaadmin/keys/ipaadmin-changepw.keytab",
		"-s",
		"127.0.0.1",
		"-x",
		"ipa-setup-override-restrictions",
		NULL
	};
*/
	char *c = "/bin/cat";
	char *a[] = { "cat", NULL };

	/* Environment. */
	char *e[] = { NULL };

	/* Lines to pass. Three lines and the NULL. */
	char *lines[4];

	/* Line. */
	char line[LINELENGTH];

	/* Status code. */
	int status;

	/* Declare signal handlers. We don't save the old ones.
	   We need SIGCHLD for the fork. */
	sig (SIGCHLD, handchld);
	sig (SIGTTOU, SIG_IGN);
	sig (SIGTTIN, SIG_IGN);
	sig (SIGTRAP, SIG_IGN);
	sig (SIGPIPE, handpipe);
	sig (SIGHUP, sighand);
	sig (SIGTERM, sighand);
	sig (SIGQUIT, sighand);
	sig (SIGUSR1, sighand);

	/* Checks. */
	if (strlen (user) > KRB_LIMIT)
	{
		error ("Username too long");
	}
	if (aliens (user, USERCHARS))
	{
		error ("Alien characters in username");
	}
	if (strlen (password) > PASSWORD_LIMIT)
	{
		error ("Password too long");
	}
	if (aliens (password, PASSCHARS))
	{
		error ("Alien characters in password");
	}

	/* Build lines passed to kadmin. */

	/* Command CPW USERNAME. */
	*line = EOS;
	strcpy (line, "cpw ");
	strcat (line, user);
	strcat (line, NEWLINESTRING);
	lines[0] = strdup (line);

	/* Password. */
	*line = EOS;
	strcpy (line, password);
	strcat (line, NEWLINESTRING);
	lines[1] = strdup (line);

	/* Password verification. */
	*line = EOS;
	strcpy (line, password);
	strcat (line, NEWLINESTRING);
	lines[2] = strdup (line);

	/* NULL at the end. */
	lines[3] = NULL;

	/* Execute command. */
	status = cmd (c, a, e, lines);

	/* Finish. */
	zerofree (lines[0]);
	zerofree (lines[1]);
	zerofree (lines[2]);
	return (status);
}

/* Main program. */

int
main (int argc, char *argv[], char *envp[])
{

	/* Username and password from the argument list. */
	char *user = NULL;
	char *password = NULL;

	/* Status code. */
	int status;

	/* Check arguments. */
	if (argc != 3)
	{
		error ("You have to specify username and password");
	}
	user = argv[1];
	if ((strstr (user, "s_") != user) &&
		(strstr (user, "t_") != user))
	{
		error ("Username does not look like a SweStore username");
	}
	password = argv[2];

	/* Execute password change command using kadmin. */
	status = cpw (user, password);
	exit (status);
}

/* End of file CMD.C. */

