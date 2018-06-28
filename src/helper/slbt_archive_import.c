/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2018  Z. Gilboa                            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>

#include <slibtool/slibtool.h>
#include "slibtool_spawn_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_symlink_impl.h"
#include "slibtool_readlink_impl.h"
#include "slibtool_errinfo_impl.h"

static char * slbt_mri_argument(
	char *	arg,
	char *	buf)
{
	int	i;
	char *	lnk;
	char *	target;
	char 	mricwd[PATH_MAX];
	char 	dstbuf[PATH_MAX];

	if ((!(strchr(arg,'+'))) && (!(strchr(arg,'-'))))
		return arg;

	if (arg[0] == '/')
		target = arg;
	else {
		if (!(getcwd(mricwd,sizeof(mricwd))))
			return 0;

		if ((size_t)snprintf(dstbuf,sizeof(dstbuf),"%s/%s",
				mricwd,arg) >= sizeof(dstbuf))
			return 0;

		target = dstbuf;
	}

	for (i=0,lnk=0; i<1024 && !lnk; i++) {
		if (!(tmpnam(buf)))
			return 0;

		if (!(symlink(target,buf)))
			lnk = buf;
	}

	return lnk;
}

static void slbt_archive_import_child(
	char *	program,
	int	fd[2])
{
	char *	argv[3];

	argv[0] = program;
	argv[1] = "-M";
	argv[2] = 0;

	close(fd[1]);

	if (dup2(fd[0],0) == 0)
		execvp(program,argv);

	_exit(EXIT_FAILURE);
}

int slbt_archive_import(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				dstarchive,
	char *				srcarchive)
{
	pid_t	pid;
	pid_t	rpid;
	int	fd[2];
	char *	dst;
	char *	src;
	char *	fmt;
	char	mridst [L_tmpnam];
	char	mrisrc [L_tmpnam];
	char	program[PATH_MAX];

	/* not needed? */
	if (slbt_symlink_is_a_placeholder(srcarchive))
		return 0;

	/* program */
	if ((size_t)snprintf(program,sizeof(program),
				"%s",
				dctx->cctx->host.ar)
			>= sizeof(program))
		return SLBT_BUFFER_ERROR(dctx);

	/* fork */
	if (pipe(fd))
		return SLBT_SYSTEM_ERROR(dctx);

	if ((pid = fork()) < 0) {
		close(fd[0]);
		close(fd[1]);
		return SLBT_SYSTEM_ERROR(dctx);
	}

	/* child */
	if (pid == 0)
		slbt_archive_import_child(
			program,
			fd);

	/* parent */
	close(fd[0]);

	ectx->pid = pid;

	dst = slbt_mri_argument(dstarchive,mridst);
	src = slbt_mri_argument(srcarchive,mrisrc);
	fmt = "OPEN %s\n"
	      "ADDLIB %s\n"
	      "SAVE\n"
	      "END\n";

	if (slbt_dprintf(fd[1],fmt,dst,src) < 0) {
		close(fd[1]);
		return SLBT_SYSTEM_ERROR(dctx);
	}

	close(fd[1]);

	rpid = waitpid(
		pid,
		&ectx->exitcode,
		0);

	if (dst == mridst)
		unlink(dst);

	if (src == mrisrc)
		unlink(src);

	return (rpid == pid) && (ectx->exitcode == 0)
		? 0 : SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_ARCHIVE_IMPORT);
}
