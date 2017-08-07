/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2017  Z. Gilboa                            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <slibtool/slibtool.h>
#include "slibtool_spawn_impl.h"
#include "slibtool_mkdir_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_metafile_impl.h"
#include "slibtool_readlink_impl.h"
#include "slibtool_symlink_impl.h"

struct slbt_deps_meta {
	char ** altv;
	char *	args;
	int	depscnt;
	int	infolen;
};

/*******************************************************************/
/*                                                                 */
/* -o <ltlib>  switches              input   result                */
/* ----------  --------------------- -----   ------                */
/* libfoo.a    [-shared|-static]     bar.lo  libfoo.a              */
/*                                                                 */
/* ar crs libfoo.a bar.o                                           */
/*                                                                 */
/*******************************************************************/

/*******************************************************************/
/*                                                                 */
/* -o <ltlib>  switches              input   result                */
/* ----------  --------------------- -----   ------                */
/* libfoo.la   -shared               bar.lo  libfoo.la             */
/*                                           .libs/libfoo.a        */
/*                                           .libs/libfoo.la (lnk) */
/*                                                                 */
/* ar crs .libs/libfoo.a .libs/bar.o                               */
/* (generate libfoo.la)                                            */
/* ln -s ../libfoo.la .libs/libfoo.la                              */
/*                                                                 */
/*******************************************************************/

/*******************************************************************/
/*                                                                 */
/* -o <ltlib>  switches              input   result                */
/* ----------  --------------------- -----   ------                */
/* libfoo.la   -static               bar.lo  libfoo.la             */
/*                                           .libs/libfoo.a        */
/*                                           .libs/libfoo.la (lnk) */
/*                                                                 */
/* ar crs .libs/libfoo.a bar.o                                     */
/* (generate libfoo.la)                                            */
/* ln -s ../libfoo.la .libs/libfoo.la                              */
/*                                                                 */
/*******************************************************************/

static int slbt_exec_link_exit(
	struct slbt_deps_meta *	depsmeta,
	int			ret)
{
	if (depsmeta->altv)
		free(depsmeta->altv);

	if (depsmeta->args)
		free(depsmeta->args);

	return ret;
}

static int slbt_get_deps_meta(
	const struct slbt_driver_ctx *	dctx,
	char *			libfilename,
	struct slbt_deps_meta *	depsmeta)
{
	int		ret;
	FILE *		fdeps;
	struct stat	st;
	char *		deplib;
	char		depfile[4*PATH_MAX];
	char *		deplibs = depfile;
	char *		base;
	size_t		libexlen;

	(void)dctx;

	/* -rpath */
	if ((size_t)snprintf(depfile,sizeof(depfile),"%s.slibtool.rpath",
				libfilename)
			>= sizeof(depfile))
		return SLBT_BUFFER_ERROR(dctx);

	if (!(lstat(depfile,&st))) {
		/* -Wl,%s */
		depsmeta->infolen += st.st_size + 4;
		depsmeta->infolen++;
	}

	/* .deps */
	if ((size_t)snprintf(depfile,sizeof(depfile),"%s.slibtool.deps",
				libfilename)
			>= sizeof(depfile))
		return SLBT_BUFFER_ERROR(dctx);

	if ((stat(depfile,&st)))
		return SLBT_SYSTEM_ERROR(dctx);

	if (!(fdeps = fopen(depfile,"r")))
		return SLBT_SYSTEM_ERROR(dctx);

	if ((size_t)st.st_size >= sizeof(depfile))
		if (!(deplibs = malloc(st.st_size+1))) {
			fclose(fdeps);
			return SLBT_SYSTEM_ERROR(dctx);
		}

	depsmeta->infolen += st.st_size;
	depsmeta->infolen++;

	deplib = st.st_size
		? fgets(deplibs,st.st_size+1,fdeps)
		: 0;

	if ((base = strrchr(libfilename,'/')))
		libexlen = strlen(depfile) + base - libfilename + 2;
	else
		libexlen = strlen(depfile) + 2;

	for (; deplib; ) {
		depsmeta->infolen += libexlen;
		depsmeta->depscnt++;
		deplib = fgets(deplibs,st.st_size+1,fdeps);
	}

	if (deplibs != depfile)
		free(deplibs);

	ret = ferror(fdeps) ? SLBT_FILE_ERROR(dctx) : 0;
	fclose(fdeps);

	return ret;
}

static bool slbt_adjust_input_argument(
	char *		arg,
	const char *	osuffix,
	const char *	asuffix,
	bool		fpic)
{
	char *	slash;
	char *	dot;
	char	base[PATH_MAX];

	if (*arg == '-')
		return false;

	if (!(dot = strrchr(arg,'.')))
		return false;

	if (strcmp(dot,osuffix))
		return false;

	if (fpic) {
		if ((slash = strrchr(arg,'/')))
			slash++;
		else
			slash = arg;

		if ((size_t)snprintf(base,sizeof(base),"%s",
				slash) >= sizeof(base))
			return false;

		sprintf(slash,".libs/%s",base);
		dot = strrchr(arg,'.');
	}

	strcpy(dot,asuffix);
	return true;
}

static int slbt_adjust_linker_argument(
	const struct slbt_driver_ctx *	dctx,
	char *		arg,
	bool		fpic,
	const char *	dsosuffix,
	const char *	arsuffix,
	struct slbt_deps_meta * depsmeta)
{
	int	fdlib;
	char *	slash;
	char *	dot;
	char	base[PATH_MAX];
	char	slnk[PATH_MAX];

	if (*arg == '-')
		return 0;

	if (!(dot = strrchr(arg,'.')))
		return 0;

	if (strcmp(dot,".la"))
		return 0;

	if (fpic) {
		if ((slash = strrchr(arg,'/')))
			slash++;
		else
			slash = arg;

		if ((size_t)snprintf(base,sizeof(base),"%s",
				slash) >= sizeof(base))
			return 0;

		sprintf(slash,".libs/%s",base);
		dot = strrchr(arg,'.');
	}

	/* shared library dependency? */
	if (fpic) {
		sprintf(dot,"%s",dsosuffix);

		if (!slbt_readlink(arg,slnk,sizeof(slnk))
				&& !(strcmp(slnk,"/dev/null")))
			sprintf(dot,"%s",arsuffix);
		else if ((fdlib = open(arg,O_RDONLY)) >= 0)
			close(fdlib);
		else
			sprintf(dot,"%s",arsuffix);

		return slbt_get_deps_meta(dctx,arg,depsmeta);
	}

	/* input archive */
	sprintf(dot,"%s",arsuffix);
	return 0;
}

static int slbt_exec_link_adjust_argument_vector(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	struct slbt_deps_meta *		depsmeta,
	const char *			cwd,
	bool				flibrary)
{
	char ** carg;
	char ** aarg;
	char *	ldir;
	char *	slash;
	char *	mark;
	char *	darg;
	char *	dot;
	char *	dep;
	char *	base;
	FILE *	fdeps;
	char *	dpath;
	bool	freqd;
	int	argc;
	char	arg[PATH_MAX];
	char	lib[PATH_MAX];
	char	depdir  [PATH_MAX];
	char	rpathdir[PATH_MAX];
	char	rpathlnk[PATH_MAX];
	bool	fwholearchive = false;
	struct	stat st;
	int	fd;

	for (argc=0,carg=ectx->cargv; *carg; carg++)
		argc++;

	if (!(depsmeta->args = calloc(1,depsmeta->infolen)))
		return SLBT_SYSTEM_ERROR(dctx);

	argc *= 3;
	argc += depsmeta->depscnt;

	if (!(depsmeta->altv = calloc(argc,sizeof(char *))))
		return SLBT_SYSTEM_ERROR(dctx);

	carg = ectx->cargv;
	aarg = depsmeta->altv;
	darg = depsmeta->args;

	for (; *carg; ) {
		dpath = 0;
		freqd = false;

		if (!strcmp(*carg,"-Wl,--whole-archive"))
			fwholearchive = true;
		else if (!strcmp(*carg,"-Wl,--no-whole-archive"))
			fwholearchive = false;



		/* output annotation */
		if (carg == ectx->lout[0]) {
			ectx->mout[0] = &aarg[0];
			ectx->mout[1] = &aarg[1];
		}

		/* argument translation */
		mark = *carg;

		if ((mark[0] == '-') && (mark[1] == 'L')) {
			if (mark[2]) {
				ldir = &mark[2];
			} else {
				*aarg++ = *carg++;
				ldir    = *carg;
			}

			mark = ldir + strlen(ldir);

			if (mark[-1] == '/')
				strcpy(mark,".libs");
			else
				strcpy(mark,"/.libs");

			if ((fd = open(ldir,O_DIRECTORY,0)) < 0)
				*mark = 0;
			else
				close(fd);

			*aarg++ = *carg++;

		} else if (**carg == '-') {
			*aarg++ = *carg++;

		} else if (!(dot = strrchr(*carg,'.'))) {
			*aarg++ = *carg++;

		} else if (!(strcmp(dot,".a"))) {
			if (flibrary && !fwholearchive)
				*aarg++ = "-Wl,--whole-archive";

			dpath = lib;
			sprintf(lib,"%s.slibtool.deps",*carg);
			*aarg++ = *carg++;

			if (flibrary && !fwholearchive)
				*aarg++ = "-Wl,--no-whole-archive";

		} else if (strcmp(dot,dctx->cctx->settings.dsosuffix)) {
			*aarg++ = *carg++;

		} else if (carg == ectx->lout[1]) {
			/* ^^^hoppla^^^ */
			*aarg++ = *carg++;
		} else {
			/* -rpath */
			sprintf(rpathlnk,"%s.slibtool.rpath",*carg);

			if (!(lstat(rpathlnk,&st))) {
				if (slbt_readlink(
						rpathlnk,\
						rpathdir,
						sizeof(rpathdir)))
					return SLBT_SYSTEM_ERROR(dctx);

				sprintf(darg,"-Wl,%s",rpathdir);
				*aarg++ = "-Wl,-rpath";
				*aarg++ = darg;
				darg   += strlen(darg);
				darg++;
			}

			dpath = lib;
			freqd = true;
			sprintf(lib,"%s.slibtool.deps",*carg);

			/* account for {'-','L','-','l'} */
			if ((size_t)snprintf(arg,sizeof(arg),"%s",
					*carg) >= (sizeof(arg) - 4))
				return SLBT_BUFFER_ERROR(dctx);

			if ((slash = strrchr(arg,'/'))) {
				sprintf(*carg,"-L%s",arg);

				mark   = strrchr(*carg,'/');
				*mark  = 0;

				if (ectx->fwrapper) {
					*slash = 0;

					if (fprintf(ectx->fwrapper,
							"DL_PATH=\"$DL_PATH$COLON%s/%s\"\n"
							"COLON=':'\n\n",
							cwd,arg) < 0)
						return SLBT_SYSTEM_ERROR(dctx);
				}

				*aarg++ = *carg++;
				*aarg++ = ++mark;

				++slash;
				slash += strlen(dctx->cctx->settings.dsoprefix);

				sprintf(mark,"-l%s",slash);
				dot  = strrchr(mark,'.');
				*dot = 0;
			} else {
				*aarg++ = *carg++;
			}
		}

		if (dpath) {
			if (!stat(dpath,&st) && (fdeps = fopen(dpath,"r"))) {
				dep = st.st_size
					? fgets(darg,st.st_size+1,fdeps)
					: 0;

				if (!(strncmp(lib,".libs/",6))) {
					*aarg++ = "-L.libs";
					lib[1] = 0;
				} else if ((base = strrchr(lib,'/'))) {
					if (base - lib == 5) {
						if (!(strncmp(&base[-5],".libs/",6)))
							base -= 4;

					} else if (base - lib >= 6) {
						if (!(strncmp(&base[-6],"/.libs/",7)))
							base -= 6;
					}

					*base = 0;
				} else {
					lib[0] = '.';
					lib[1] = 0;
				}

				for (; dep; ) {
					*aarg++ = darg;
					mark    = darg;
					darg   += strlen(dep);

					if (darg[-1] == '\n')
						darg[-1] = 0;
					else
						darg++;

					/* handle -L... as needed */
					if ((mark[0] == '-')
							&& (mark[1] == 'L')
							&& (mark[2] != '/')) {
						if (strlen(mark) >= sizeof(depdir) - 1)
							return SLBT_BUFFER_ERROR(dctx);

						darg = mark;
						strcpy(depdir,&mark[2]);
						sprintf(darg,"-L%s/%s",lib,depdir);

						darg += strlen(darg);
						darg++;
					}

					dep = fgets(darg,st.st_size+1,fdeps);
				}

				if (ferror(fdeps)) {
					free(depsmeta->altv);
					free(depsmeta->args);
					fclose(fdeps);
					return SLBT_FILE_ERROR(dctx);
				} else {
					fclose(fdeps);
				}
			} else if (freqd) {
				free(depsmeta->altv);
				free(depsmeta->args);
				return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LINK_FREQ);
			}
		}
	}

	return 0;
}

static int slbt_exec_link_remove_file(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			target)
{
	(void)ectx;

	/* remove target (if any) */
	if (!(unlink(target)) || (errno == ENOENT))
		return 0;

	return SLBT_SYSTEM_ERROR(dctx);
}

static int slbt_exec_link_create_dep_file(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *	ectx,
	char **			altv,
	const char *		libfilename,
	bool			farchive)
{
	char **	parg;
	char *	popt;
	char *	plib;
	char *	mark;
	char *	base;
	size_t	size;
	FILE *	fdeps;
	char *	deplib;
	char	reladir[PATH_MAX];
	char	deplibs[PATH_MAX];
	char	depfile[PATH_MAX];
	struct  stat st;
	int	ldepth;
	int	fdyndep;

	(void)dctx;

	if (ectx->fdeps)
		fclose(ectx->fdeps);

	if ((size_t)snprintf(depfile,sizeof(depfile),"%s.slibtool.deps",
				libfilename)
			>= sizeof(depfile))
		return SLBT_BUFFER_ERROR(dctx);

	if (!(ectx->fdeps = fopen(depfile,"w")))
		return SLBT_SYSTEM_ERROR(dctx);

	for (parg=altv; *parg; parg++) {
		popt = 0;
		plib = 0;

		if (!strcmp(*parg,"-l")) {
			popt = *parg++;
			plib = *parg;
		} else if (!strcmp(*parg,"--library")) {
			popt = *parg++;
			plib = *parg;
		} else if (!strncmp(*parg,"-l",2)) {
			popt = *parg;
			plib = popt + 2;
		} else if (!strncmp(*parg,"--library=",10)) {
			popt = *parg;
			plib = popt + 10;
		} else if (!strncmp(*parg,"-f",2)) {
			(void)0;
		} else if ((popt = strrchr(*parg,'.')) && !strcmp(popt,".la")) {
			/* import dependency list */
			if ((base = strrchr(*parg,'/')))
				base++;
			else
				base = *parg;

			/* [relative .la directory] */
			if (base > *parg) {
				if ((size_t)snprintf(reladir,
							sizeof(reladir),
							"%s",*parg)
						>= sizeof(reladir)) {
					fclose(fdeps);
					return SLBT_SYSTEM_ERROR(dctx);
				}

				reladir[base - *parg - 1] = 0;
			} else {
				reladir[0] = '.';
				reladir[1] = 0;
			}


			/* dynamic library dependency? */
			strcpy(depfile,*parg);
			mark = depfile + (base - *parg);
			size = sizeof(depfile) - (base - *parg);

			if ((size_t)snprintf(mark,size,".libs/%s",base)
					>= size)
				return SLBT_BUFFER_ERROR(dctx);

			mark = strrchr(mark,'.');
			strcpy(mark,dctx->cctx->settings.dsosuffix);

			fdyndep = !stat(depfile,&st);

			/* [-L... as needed] */
			if (fdyndep && (base > *parg) && (ectx->ldirdepth >= 0)) {
				if (fputs("-L",ectx->fdeps) < 0) {
					fclose(fdeps);
					return SLBT_SYSTEM_ERROR(dctx);
				}

				for (ldepth=ectx->ldirdepth; ldepth; ldepth--)
					if (fputs("../",ectx->fdeps) < 0) {
						fclose(fdeps);
						return SLBT_SYSTEM_ERROR(dctx);
					}

				if (fprintf(ectx->fdeps,"%s/.libs\n",reladir) < 0) {
					fclose(fdeps);
					return SLBT_SYSTEM_ERROR(dctx);
				}
			}

			/* -ldeplib */
			if (fdyndep) {
				*popt = 0;
				mark  = base;
				mark += strlen(dctx->cctx->settings.dsoprefix);

				if (fprintf(ectx->fdeps,"-l%s\n",mark) < 0) {
					fclose(fdeps);
					return SLBT_SYSTEM_ERROR(dctx);
				}

				*popt = '.';
			}

			/* [open dependency list] */
			strcpy(depfile,*parg);
			mark = depfile + (base - *parg);
			size = sizeof(depfile) - (base - *parg);

			if ((size_t)snprintf(mark,size,".libs/%s",base)
					>= size)
				return SLBT_BUFFER_ERROR(dctx);

			mark = strrchr(mark,'.');
			size = sizeof(depfile) - (mark - depfile);

			if (!farchive) {
				if ((size_t)snprintf(
						mark,size,
						"%s.slibtool.deps",
						dctx->cctx->settings.dsosuffix)
						>= size)
					return SLBT_BUFFER_ERROR(dctx);

				if (stat(depfile,&st)) {
					if (errno == ENOENT)
						fdeps = 0;
					else
						return SLBT_SYSTEM_ERROR(dctx);
				} else {
					if (!(fdeps = fopen(depfile,"r")))
						return SLBT_SYSTEM_ERROR(dctx);
				}
			}

			if (farchive || !fdeps) {
				if ((size_t)snprintf(mark,size,".a.slibtool.deps")
						>= size)
					return SLBT_BUFFER_ERROR(dctx);

				if (stat(depfile,&st))
					return SLBT_SYSTEM_ERROR(dctx);

				if (!(fdeps = fopen(depfile,"r")))
					return SLBT_SYSTEM_ERROR(dctx);
			}

			/* [-l... as needed] */
			deplib = st.st_size
				? fgets(deplibs,st.st_size+1,fdeps)
				: 0;

			for (; deplib; ) {
				if ((deplib[0] == '-') && (deplib[1] == 'L')
						&& (deplib[2] != '/')) {
					if (fprintf(ectx->fdeps,"-L%s/%s",
							reladir,&deplib[2]) < 0) {
						fclose(fdeps);
						return SLBT_SYSTEM_ERROR(dctx);
					}
				} else {
					if (fprintf(ectx->fdeps,"%s",deplib) < 0) {
						fclose(fdeps);
						return SLBT_SYSTEM_ERROR(dctx);
					}
				}

				deplib = fgets(deplibs,st.st_size+1,fdeps);
			}

			fclose(fdeps);
		}

		if (plib)
			if (fprintf(ectx->fdeps,"-l%s\n",plib) < 0)
				return SLBT_SYSTEM_ERROR(dctx);
	}

	if (fflush(ectx->fdeps))
		return SLBT_SYSTEM_ERROR(dctx);

	return 0;
}

static int slbt_exec_link_create_import_library(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				impfilename,
	char *				deffilename,
	char *				soname,
	bool				ftag)
{
	char *	slash;
	char *	dlltool[8];
	char	program[PATH_MAX];
	char	hosttag[PATH_MAX];
	char	hostlnk[PATH_MAX];

	/* libfoo.so.def.{flavor} */
	if (ftag) {
		if ((size_t)snprintf(hosttag,sizeof(hosttag),"%s.%s",
				deffilename,
				dctx->cctx->host.flavor) >= sizeof(hosttag))
			return SLBT_BUFFER_ERROR(dctx);

		if ((size_t)snprintf(hostlnk,sizeof(hostlnk),"%s.host",
				deffilename) >= sizeof(hostlnk))
			return SLBT_BUFFER_ERROR(dctx);

		/* libfoo.so.def is under .libs/ */
		if (!(slash = strrchr(deffilename,'/')))
			return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LINK_FLOW);

		if (slbt_create_symlink(
				dctx,ectx,
				deffilename,
				hosttag,
				false))
			return SLBT_NESTED_ERROR(dctx);

		/* libfoo.so.def.{flavor} is under .libs/ */
		if (!(slash = strrchr(hosttag,'/')))
			return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LINK_FLOW);

		if (slbt_create_symlink(
				dctx,ectx,
				++slash,
				hostlnk,
				false))
			return SLBT_NESTED_ERROR(dctx);
	}

	/* dlltool argv */
	if ((size_t)snprintf(program,sizeof(program),"%s",
			dctx->cctx->host.dlltool) >= sizeof(program))
		return SLBT_BUFFER_ERROR(dctx);

	dlltool[0] = program;
	dlltool[1] = "-l";
	dlltool[2] = impfilename;
	dlltool[3] = "-d";
	dlltool[4] = deffilename;
	dlltool[5] = "-D";
	dlltool[6] = soname;
	dlltool[7] = 0;

	/* alternate argument vector */
	ectx->argv    = dlltool;
	ectx->program = program;

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
			return SLBT_NESTED_ERROR(dctx);

	/* dlltool spawn */
	if ((slbt_spawn(ectx,true) < 0) || ectx->exitcode)
		return SLBT_SPAWN_ERROR(dctx);

	return 0;
}

static int slbt_exec_link_create_noop_symlink(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			arfilename)
{
	struct stat st;

	/* file exists? */
	if (!(lstat(arfilename,&st)))
		return 0;

	/* needed? */
	if (errno == ENOENT) {
		if (slbt_create_symlink(
				dctx,ectx,
				"/dev/null",
				arfilename,
                                false))
			return SLBT_NESTED_ERROR(dctx);
		return 0;
	}

	return SLBT_SYSTEM_ERROR(dctx);
}

static int slbt_exec_link_create_archive(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			arfilename,
	bool				fpic,
	bool				fprimary)
{
	char ** 	aarg;
	char ** 	parg;
	char *		base;
	char *		mark;
	char *		slash;
	char		program[PATH_MAX];
	char		output [PATH_MAX];
	char		arfile [PATH_MAX];
	char		arlink [PATH_MAX];

	/* -disable-static? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_STATIC)
		if (dctx->cctx->rpath)
			return slbt_exec_link_create_noop_symlink(
				dctx,ectx,arfilename);

	/* initial state */
	slbt_reset_arguments(ectx);

	/* placeholders */
	slbt_reset_placeholders(ectx);

	/* alternate program (ar, ranlib) */
	ectx->program = program;

	/* output */
	if ((size_t)snprintf(output,sizeof(output),"%s",
			arfilename) >= sizeof(output))
		return SLBT_BUFFER_ERROR(dctx);

	/* ar alternate argument vector */
	if ((size_t)snprintf(program,sizeof(program),"%s",
			dctx->cctx->host.ar) >= sizeof(program))
		return SLBT_BUFFER_ERROR(dctx);

	aarg    = ectx->altv;
	*aarg++ = program;
	*aarg++ = "crs";
	*aarg++ = output;

	/* input argument adjustment */
	for (parg=ectx->cargv; *parg; parg++)
		if (slbt_adjust_input_argument(*parg,".lo",".o",fpic))
			*aarg++ = *parg;

	*aarg = 0;
	ectx->argv = ectx->altv;

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
			return SLBT_NESTED_ERROR(dctx);

	/* remove old archive as needed */
	if (slbt_exec_link_remove_file(dctx,ectx,output))
		return SLBT_NESTED_ERROR(dctx);

	/* .deps */
	if (slbt_exec_link_create_dep_file(
			dctx,ectx,ectx->cargv,
			arfilename,true))
		return SLBT_NESTED_ERROR(dctx);

	/* ar spawn */
	if ((slbt_spawn(ectx,true) < 0) || ectx->exitcode)
		return SLBT_SPAWN_ERROR(dctx);

	/* input objects associated with .la archives */
	for (parg=ectx->cargv; *parg; parg++)
		if (slbt_adjust_input_argument(*parg,".la",".a",true))
			if (slbt_archive_import(dctx,ectx,output,*parg))
				return SLBT_NESTED_ERROR(dctx);

	if (fprimary && (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_SHARED)) {
		strcpy(arlink,output);
		mark  = strrchr(arlink,'/');
		*mark = 0;

		base  = output + (mark - arlink);
		base++;

		if ((slash = strrchr(arlink,'/')))
			slash++;
		else
			slash = arlink;

		strcpy(slash,base);
		sprintf(arfile,".libs/%s",base);

		if (slbt_exec_link_remove_file(dctx,ectx,arlink))
			return SLBT_NESTED_ERROR(dctx);

		if (symlink(arfile,arlink))
			return SLBT_SYSTEM_ERROR(dctx);
	}

	return 0;
}

static int slbt_exec_link_create_library(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			dsofilename,
	const char *			relfilename)
{
	char ** parg;
	char	cwd    [PATH_MAX];
	char	output [PATH_MAX];
	char	soname [PATH_MAX];
	char	symfile[PATH_MAX];
	struct slbt_deps_meta depsmeta = {0,0,0,0};

	/* initial state */
	slbt_reset_arguments(ectx);

	/* placeholders */
	slbt_reset_placeholders(ectx);

	/* input argument adjustment */
	for (parg=ectx->cargv; *parg; parg++)
		slbt_adjust_input_argument(*parg,".lo",".o",true);

	/* .deps */
	if (slbt_exec_link_create_dep_file(
			dctx,ectx,ectx->cargv,
			dsofilename,false))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_NESTED_ERROR(dctx));

	/* linker argument adjustment */
	for (parg=ectx->cargv; *parg; parg++)
		if (slbt_adjust_linker_argument(
				dctx,
				*parg,true,
				dctx->cctx->settings.dsosuffix,
				dctx->cctx->settings.arsuffix,
				&depsmeta) < 0)
			return SLBT_NESTED_ERROR(dctx);

	/* --no-undefined */
	if (dctx->cctx->drvflags & SLBT_DRIVER_NO_UNDEFINED)
		*ectx->noundef = "-Wl,--no-undefined";

	/* -soname */
	if ((dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_MACHO)) {
		(void)0;
	} else if (!(dctx->cctx->drvflags & SLBT_DRIVER_AVOID_VERSION)) {
		if ((size_t)snprintf(soname,sizeof(soname),"-Wl,%s%s%s.%d",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->settings.dsosuffix,
					dctx->cctx->verinfo.major)
				>= sizeof(soname))
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = "-Wl,-soname";
		*ectx->lsoname = soname;
	} else if (relfilename) {
		if ((size_t)snprintf(soname,sizeof(soname),"-Wl,%s%s-%s%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dctx->cctx->settings.dsosuffix)
				>= sizeof(soname))
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = "-Wl,-soname";
		*ectx->lsoname = soname;
	}

	/* PE: --output-def */
	if (dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_PE) {
		if ((size_t)snprintf(symfile,sizeof(symfile),"-Wl,%s",
					ectx->deffilename)
				>= sizeof(output))
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->symdefs = "-Wl,--output-def";
		*ectx->symfile = symfile;
	}

	/* shared/static */
	if (dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC) {
		*ectx->dpic = "-static";
	} else {
		*ectx->dpic = "-shared";
		*ectx->fpic = "-fPIC";
	}

	/* output */
	if (relfilename) {
		strcpy(output,relfilename);
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_AVOID_VERSION) {
		strcpy(output,dsofilename);
	} else {
		if ((size_t)snprintf(output,sizeof(output),"%s.%d.%d.%d",
					dsofilename,
					dctx->cctx->verinfo.major,
					dctx->cctx->verinfo.minor,
					dctx->cctx->verinfo.revision)
				>= sizeof(output))
			return SLBT_BUFFER_ERROR(dctx);
	}

	*ectx->lout[0] = "-o";
	*ectx->lout[1] = output;

	/* ldrpath */
	if (dctx->cctx->host.ldrpath) {
		if (slbt_exec_link_remove_file(dctx,ectx,ectx->rpathfilename))
			return SLBT_NESTED_ERROR(dctx);

		if (symlink(dctx->cctx->host.ldrpath,ectx->rpathfilename))
			return SLBT_SYSTEM_ERROR(dctx);
	}

	/* cwd */
	if (!getcwd(cwd,sizeof(cwd)))
		return SLBT_SYSTEM_ERROR(dctx);

	/* .libs/libfoo.so --> -L.libs -lfoo */
	if (slbt_exec_link_adjust_argument_vector(
			dctx,ectx,&depsmeta,cwd,true))
		return SLBT_NESTED_ERROR(dctx);

	/* using alternate argument vector */
	ectx->argv    = depsmeta.altv;
	ectx->program = depsmeta.altv[0];

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
			return slbt_exec_link_exit(
				&depsmeta,
				SLBT_NESTED_ERROR(dctx));

	/* spawn */
	if ((slbt_spawn(ectx,true) < 0) || ectx->exitcode)
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SPAWN_ERROR(dctx));

	return slbt_exec_link_exit(&depsmeta,0);
}

static int slbt_exec_link_create_executable(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			exefilename)
{
	char ** parg;
	char *	base;
	char	cwd    [PATH_MAX];
	char	output [PATH_MAX];
	char	wrapper[PATH_MAX];
	char	wraplnk[PATH_MAX];
	bool	fabspath;
	bool	fpic;
	const struct slbt_source_version * verinfo;
	struct slbt_deps_meta depsmeta = {0,0,0,0};

	/* initial state */
	slbt_reset_arguments(ectx);

	/* placeholders */
	slbt_reset_placeholders(ectx);

	/* fpic */
	fpic = !(dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC);

	/* input argument adjustment */
	for (parg=ectx->cargv; *parg; parg++)
		slbt_adjust_input_argument(*parg,".lo",".o",fpic);

	/* linker argument adjustment */
	for (parg=ectx->cargv; *parg; parg++)
		if (slbt_adjust_linker_argument(
				dctx,
				*parg,true,
				dctx->cctx->settings.dsosuffix,
				dctx->cctx->settings.arsuffix,
				&depsmeta) < 0)
			return SLBT_NESTED_ERROR(dctx);

	/* --no-undefined */
	if (dctx->cctx->drvflags & SLBT_DRIVER_NO_UNDEFINED)
		*ectx->noundef = "-Wl,--no-undefined";

	/* executable wrapper: header */
	if ((size_t)snprintf(wrapper,sizeof(wrapper),"%s.wrapper.tmp",
				dctx->cctx->output)
			>= sizeof(wrapper))
		return SLBT_BUFFER_ERROR(dctx);

	if (!(ectx->fwrapper = fopen(wrapper,"w")))
		return SLBT_SYSTEM_ERROR(dctx);

	verinfo = slbt_source_version();

	if (fprintf(ectx->fwrapper,
			"#!/bin/sh\n"
			"# libtool compatible executable wrapper\n"
			"# Generated by %s (slibtool %d.%d.%d)\n"
			"# [commit reference: %s]\n\n"

			"if [ -z \"$%s\" ]; then\n"
			"\tDL_PATH=\n"
			"\tCOLON=\n"
			"\tLCOLON=\n"
			"else\n"
			"\tDL_PATH=\n"
			"\tCOLON=\n"
			"\tLCOLON=':'\n"
			"fi\n\n",

			dctx->program,
			verinfo->major,verinfo->minor,verinfo->revision,
			verinfo->commit,
			dctx->cctx->settings.ldpathenv) < 0)
		return SLBT_SYSTEM_ERROR(dctx);

	/* output */
	if ((size_t)snprintf(output,sizeof(output),"%s",
				exefilename)
			>= sizeof(output))
		return SLBT_BUFFER_ERROR(dctx);

	*ectx->lout[0] = "-o";
	*ectx->lout[1] = output;

	/* static? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC)
		*ectx->dpic = "-static";

	/* cwd */
	if (!getcwd(cwd,sizeof(cwd)))
		return SLBT_SYSTEM_ERROR(dctx);

	/* .libs/libfoo.so --> -L.libs -lfoo */
	if (slbt_exec_link_adjust_argument_vector(
			dctx,ectx,&depsmeta,cwd,false))
		return SLBT_NESTED_ERROR(dctx);

	/* using alternate argument vector */
	ectx->argv    = depsmeta.altv;
	ectx->program = depsmeta.altv[0];

	/* executable wrapper symlink */
	if ((size_t)snprintf(wraplnk,sizeof(wraplnk),"%s.exe.wrapper",
			dctx->cctx->output) >= sizeof(wraplnk))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_BUFFER_ERROR(dctx));

	/* executable wrapper: base name */
	if ((base = strrchr(wraplnk,'/')))
		base++;
	else
		base = wraplnk;

	/* executable wrapper: footer */
	fabspath = (exefilename[0] == '/');

	if (fprintf(ectx->fwrapper,
			"DL_PATH=\"$DL_PATH$LCOLON$%s\"\n\n"
			"export %s=$DL_PATH\n\n"
			"if [ `basename \"$0\"` = \"%s\" ]; then\n"
			"\tprogram=\"$1\"; shift\n"
			"\texec \"$program\" \"$@\"\n"
			"fi\n\n"
			"exec %s/%s \"$@\"\n",
			dctx->cctx->settings.ldpathenv,
			dctx->cctx->settings.ldpathenv,
			base,
			fabspath ? "" : cwd,
			fabspath ? &exefilename[1] : exefilename) < 0)
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx));

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
			return slbt_exec_link_exit(
				&depsmeta,
				SLBT_NESTED_ERROR(dctx));

	/* spawn */
	if ((slbt_spawn(ectx,true) < 0) || ectx->exitcode)
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SPAWN_ERROR(dctx));

	/* executable wrapper: finalize */
	fclose(ectx->fwrapper);
	ectx->fwrapper = 0;

	if (slbt_create_symlink(
			dctx,ectx,
			dctx->cctx->output,wraplnk,
			false))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_NESTED_ERROR(dctx));

	if (rename(wrapper,dctx->cctx->output))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx));

	if (chmod(dctx->cctx->output,0755))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx));

	return slbt_exec_link_exit(&depsmeta,0);
}

static int slbt_exec_link_create_library_symlink(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	bool				fmajor)
{
	char	target[PATH_MAX];
	char	lnkname[PATH_MAX];

	if (ectx->relfilename) {
		strcpy(target,ectx->relfilename);
		sprintf(lnkname,"%s.release",ectx->dsofilename);

		if (slbt_create_symlink(
				dctx,ectx,
				target,lnkname,
				false))
			return SLBT_NESTED_ERROR(dctx);
	} else
		sprintf(target,"%s.%d.%d.%d",
			ectx->dsofilename,
			dctx->cctx->verinfo.major,
			dctx->cctx->verinfo.minor,
			dctx->cctx->verinfo.revision);

	if (fmajor)
		sprintf(lnkname,"%s.%d",
			ectx->dsofilename,
			dctx->cctx->verinfo.major);

	else
		strcpy(lnkname,ectx->dsofilename);

	if (fmajor && (dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_PE))
		return slbt_copy_file(
			dctx,ectx,
			target,lnkname);
	else
		return slbt_create_symlink(
			dctx,ectx,
			target,lnkname,
			false);
}

int slbt_exec_link(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int			ret;
	const char *		output;
	char *			dot;
	struct slbt_exec_ctx *	actx;
	bool			fpic;
	bool			fstaticonly;
	char			soname[PATH_MAX];
	char			soxyz [PATH_MAX];
	char			solnk [PATH_MAX];
	char			arname[PATH_MAX];

	/* dry run */
	if (dctx->cctx->drvflags & SLBT_DRIVER_DRY_RUN)
		return 0;

	/* context */
	if (ectx)
		actx = 0;
	else if ((ret = slbt_get_exec_ctx(dctx,&ectx)))
		return SLBT_NESTED_ERROR(dctx);
	else
		actx = ectx;

	/* libfoo.so.x.y.z */
	if ((size_t)snprintf(soxyz,sizeof(soxyz),"%s%s%s.%d.%d.%d",
				ectx->sonameprefix,
				dctx->cctx->libname,
				dctx->cctx->settings.dsosuffix,
				dctx->cctx->verinfo.major,
				dctx->cctx->verinfo.minor,
				dctx->cctx->verinfo.revision)
			>= sizeof(soxyz)) {
		slbt_free_exec_ctx(actx);
		return SLBT_BUFFER_ERROR(dctx);
	}

	/* libfoo.so.x */
	sprintf(soname,"%s%s%s.%d",
		ectx->sonameprefix,
		dctx->cctx->libname,
		dctx->cctx->settings.dsosuffix,
		dctx->cctx->verinfo.major);

	/* libfoo.so */
	sprintf(solnk,"%s%s%s",
		ectx->sonameprefix,
		dctx->cctx->libname,
		dctx->cctx->settings.dsosuffix);

	/* libfoo.a */
	sprintf(arname,"%s%s%s",
		dctx->cctx->settings.arprefix,
		dctx->cctx->libname,
		dctx->cctx->settings.arsuffix);

	/* output suffix */
	output = dctx->cctx->output;
	dot    = strrchr(output,'.');

	/* .libs directory */
	if (slbt_mkdir(ectx->ldirname)) {
		slbt_free_exec_ctx(actx);
		return SLBT_SYSTEM_ERROR(dctx);
	}

	/* non-pic libfoo.a */
	if (dot && !strcmp(dot,".a"))
		if (slbt_exec_link_create_archive(dctx,ectx,output,false,false)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}

	/* fpic, fstaticonly */
	if (dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC) {
		fstaticonly = true;
		fpic        = false;
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_SHARED) {
		fstaticonly = true;
		fpic        = false;
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_SHARED) {
		fstaticonly = false;
		fpic        = true;
	} else {
		fstaticonly = false;
		fpic        = false;
	}

	/* pic libfoo.a */
	if (dot && !strcmp(dot,".la"))
		if (slbt_exec_link_create_archive(
				dctx,ectx,
				ectx->arfilename,
				fpic,true)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}

	/* -all-static library */
	if (fstaticonly && dctx->cctx->libname)
		if (slbt_create_symlink(
				dctx,ectx,
				"/dev/null",
				ectx->dsofilename,
				false))
			return SLBT_NESTED_ERROR(dctx);

	/* dynamic library */
	if (dot && !strcmp(dot,".la") && dctx->cctx->rpath && !fstaticonly) {
		/* linking: libfoo.so.x.y.z */
		if (slbt_exec_link_create_library(
				dctx,ectx,
				ectx->dsofilename,
				ectx->relfilename)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}

		if (!(dctx->cctx->drvflags & SLBT_DRIVER_AVOID_VERSION)) {
			/* symlink: libfoo.so.x --> libfoo.so.x.y.z */
			if (slbt_exec_link_create_library_symlink(
					dctx,ectx,
					true)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}

			/* symlink: libfoo.so --> libfoo.so.x.y.z */
			if (slbt_exec_link_create_library_symlink(
					dctx,ectx,
					false)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}
		} else if (ectx->relfilename) {
			/* symlink: libfoo.so --> libfoo-x.y.z.so */
			if (slbt_exec_link_create_library_symlink(
					dctx,ectx,
					false)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}
		}

		/* PE import libraries */
		if (dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_PE) {
			/* libfoo.x.lib.a */
			if (slbt_exec_link_create_import_library(
					dctx,ectx,
					ectx->pimpfilename,
					ectx->deffilename,
					soname,
					true))
				return SLBT_NESTED_ERROR(dctx);

			/* symlink: libfoo.lib.a --> libfoo.x.lib.a */
			if (slbt_create_symlink(
					dctx,ectx,
					ectx->pimpfilename,
					ectx->dimpfilename,
					false))
				return SLBT_NESTED_ERROR(dctx);

			/* libfoo.x.y.z.lib.a */
			if (slbt_exec_link_create_import_library(
					dctx,ectx,
					ectx->vimpfilename,
					ectx->deffilename,
					soxyz,
					false))
				return SLBT_NESTED_ERROR(dctx);
		}
	}

	/* executable */
	if (!dctx->cctx->libname) {
		/* linking: .libs/exefilename */
		if (slbt_exec_link_create_executable(
				dctx,ectx,
				ectx->exefilename)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}
	}

	/* no wrapper? */
	if (!dot || strcmp(dot,".la")) {
		slbt_free_exec_ctx(actx);
		return 0;
	}

	/* library wrapper */
	if (slbt_create_library_wrapper(
			dctx,ectx,
			arname,soname,soxyz,solnk)) {
		slbt_free_exec_ctx(actx);
		return SLBT_NESTED_ERROR(dctx);
	}

	/* wrapper symlink */
	if ((ret = slbt_create_symlink(
			dctx,ectx,
			output,
			ectx->lafilename,
			true)))
		SLBT_NESTED_ERROR(dctx);

	/* .lai wrapper symlink */
	if (ret == 0)
		if ((ret = slbt_create_symlink(
				dctx,ectx,
				output,
				ectx->laifilename,
				true)))
			SLBT_NESTED_ERROR(dctx);

	/* all done */
	slbt_free_exec_ctx(actx);

	return ret;
}
