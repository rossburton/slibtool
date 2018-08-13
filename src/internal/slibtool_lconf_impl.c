/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2018  Z. Gilboa                            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "slibtool_lconf_impl.h"
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_symlink_impl.h"
#include "slibtool_readlink_impl.h"

enum slbt_lconf_opt {
	SLBT_LCONF_OPT_UNKNOWN,
	SLBT_LCONF_OPT_NO,
	SLBT_LCONF_OPT_YES,
};

static void slbt_lconf_close(int fdcwd, int fdlconfdir)
{
	if (fdlconfdir != fdcwd)
		close(fdlconfdir);
}

static int slbt_lconf_open(
	struct slbt_driver_ctx *	dctx,
	const char *			lconf)
{
	int		fdcwd;
	int		fdlconf;
	int		fdlconfdir;
	int		fdparent;
	struct stat	stcwd;
	struct stat	stparent;

	fdcwd      = slbt_driver_fdcwd(dctx);
	fdlconfdir = fdcwd;

	if (lconf)
		return ((fdlconf = openat(fdcwd,lconf,O_RDONLY,0)) < 0)
			? SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LCONF_OPEN)
			: fdlconf;

	if (fstatat(fdlconfdir,".",&stcwd,0) < 0)
		return SLBT_SYSTEM_ERROR(dctx);

	fdlconf = openat(fdlconfdir,"libtool",O_RDONLY,0);

	while (fdlconf < 0) {
		fdparent = openat(fdlconfdir,"../",O_DIRECTORY,0);
		slbt_lconf_close(fdcwd,fdlconfdir);

		if (fdparent < 0)
			return SLBT_SYSTEM_ERROR(dctx);

		if (fstat(fdparent,&stparent) < 0)
			return SLBT_SYSTEM_ERROR(dctx);

		if (stparent.st_dev != stcwd.st_dev)
			return SLBT_CUSTOM_ERROR(
				dctx,SLBT_ERR_LCONF_OPEN);

		fdlconfdir = fdparent;
		fdlconf    = openat(fdlconfdir,"libtool",O_RDONLY,0);
	}

	return fdlconf;
}

int slbt_get_lconf_flags(
	struct slbt_driver_ctx *	dctx,
	const char *			lconf,
	uint64_t *			flags)
{
	int				fdlconf;
	struct stat			st;
	void *				addr;
	const char *			mark;
	const char *			cap;
	uint64_t			optshared;
	uint64_t			optstatic;
	int				optlenmax;
	int				optsharedlen;
	int				optstaticlen;
	const char *			optsharedstr;
	const char *			optstaticstr;

	/* open relative libtool script */
	if ((fdlconf = slbt_lconf_open(dctx,lconf)) < 0)
		return SLBT_NESTED_ERROR(dctx);

	/* map relative libtool script */
	if (fstat(fdlconf,&st) < 0)
		return SLBT_SYSTEM_ERROR(dctx);

	addr = mmap(
		0,st.st_size,
		PROT_READ,MAP_SHARED,
		fdlconf,0);

	close(fdlconf);

	if (addr == MAP_FAILED)
		return SLBT_CUSTOM_ERROR(
			dctx,SLBT_ERR_LCONF_MAP);

	mark = addr;
	cap  = &mark[st.st_size];

	/* hard-coded options in the generated libtool precede the code */
	if (st.st_size >= (optlenmax = strlen("build_libtool_libs=yes\n")))
		cap -= optlenmax;

	/* scan */
	optshared = 0;
	optstatic = 0;

	optsharedstr = "build_libtool_libs=";
	optstaticstr = "build_old_libs=";

	optsharedlen = strlen(optsharedstr);
	optstaticlen = strlen(optstaticstr);

	for (; mark && mark<cap; ) {
		if (!strncmp(mark,optsharedstr,optsharedlen)) {
			mark += optsharedlen;

			if ((mark[0]=='n')
					&& (mark[1]=='o')
					&& (mark[2]=='\n'))
				optshared = SLBT_DRIVER_DISABLE_SHARED;

			if ((mark[0]=='y')
					&& (mark[1]=='e')
					&& (mark[2]=='s')
					&& (mark[3]=='\n'))
				optshared = SLBT_DRIVER_SHARED;

		} if (!strncmp(mark,optstaticstr,optstaticlen)) {
			mark += optstaticlen;

			if ((mark[0]=='n')
					&& (mark[1]=='o')
					&& (mark[2]=='\n'))
				optstatic = SLBT_DRIVER_DISABLE_STATIC;

			if ((mark[0]=='y')
					&& (mark[1]=='e')
					&& (mark[2]=='s')
					&& (mark[3]=='\n'))
				optstatic = SLBT_DRIVER_STATIC;
		}

		if (optshared && optstatic)
			mark = 0;

		else {
			for (; (mark<cap) && (*mark!='\n'); )
				mark++;
			mark++;
		}
	}

	munmap(addr,st.st_size);

	if (!optshared || !optstatic)
		return SLBT_CUSTOM_ERROR(
			dctx,SLBT_ERR_LCONF_PARSE);

	*flags = optshared | optstatic;

	return 0;
}
