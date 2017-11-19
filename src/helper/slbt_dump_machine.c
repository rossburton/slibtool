/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2017  Z. Gilboa                            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>

#include <slibtool/slibtool.h>
#include "slibtool_spawn_impl.h"

static void slbt_dump_machine_child(
	char *	program,
	int	fd[2])
{
	char *	compiler;
	char *	argv[3];

	if ((compiler = strchr(program,'/')))
		compiler++;
	else
		compiler = program;

	argv[0] = compiler;
	argv[1] = "-dumpmachine";
	argv[2] = 0;

	close(fd[0]);

	if ((fd[0] = open("/dev/null",O_RDONLY)) >= 0)
		if (dup2(fd[0],0) == 0)
			if (dup2(fd[1],1) == 1)
				execvp(program,argv);

	_exit(EXIT_FAILURE);
}

int slbt_dump_machine(
	const char *	compiler,
	char *		machine,
	size_t		bufsize)
{
	pid_t	pid;
	pid_t	rpid;
	int	code;
	int	fd[2];
	FILE *	fmachine;
	char *	newline;
	char *	mark;
	char	check[2];
	char	program[PATH_MAX];

	if (!machine || !bufsize)
		return -1;

	if ((size_t)snprintf(program,sizeof(program),"%s",
			compiler) >= sizeof(program))
		return -1;

	if (pipe(fd))
		return -1;

	if ((pid = fork()) < 0) {
		close(fd[0]);
		close(fd[1]);
		return -1;
	}

	if (pid == 0)
		slbt_dump_machine_child(
			program,
			fd);

	rpid = waitpid(
		pid,
		&code,
		0);

	if ((rpid != pid) || code) {
		close(fd[0]);
		close(fd[1]);
		return -1;
	}

	if ((fmachine = fdopen(fd[0],"r"))) {
		close(fd[1]);
		newline = 0;

		if (fgets(machine,bufsize,fmachine) == machine)
			if (!fgets(check,sizeof(check),fmachine))
				if (feof(fmachine))
					if ((newline = strrchr(machine,'\n')))
						*newline = 0;

		fclose(fmachine);
	} else {
		newline = 0;
		close(fd[0]);
		close(fd[1]);
	}

	/* support the portbld <--> unknown synonym */
	if (newline)
		if ((mark = strstr(machine,"-portbld-")))
			memcpy(mark,"-unknown",8);

	return newline ? 0 : -1;
}
