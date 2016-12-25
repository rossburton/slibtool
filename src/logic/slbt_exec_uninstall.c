/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016  Z. Gilboa                                  */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define ARGV_DRIVER

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_uninstall_impl.h"
#include "slibtool_readlink_impl.h"
#include "slibtool_errinfo_impl.h"
#include "argv/argv.h"

static int slbt_uninstall_usage(
	const char *			program,
	const char *			arg,
	const struct argv_option **	optv,
	struct argv_meta *		meta)
{
	char header[512];

	snprintf(header,sizeof(header),
		"Usage: %s --mode=uninstall <rm> [options] [DEST]...\n"
		"Options:\n",
		program);

	argv_usage(stdout,header,optv,arg);
	argv_free(meta);

	return SLBT_USAGE;
}

static int slbt_exec_uninstall_fail(
	struct slbt_exec_ctx *	actx,
	struct argv_meta *	meta,
	int			ret)
{
	argv_free(meta);
	slbt_free_exec_ctx(actx);
	return ret;
}

static int slbt_exec_uninstall_fs_entry(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char **				parg,
	char *				path,
	uint32_t			flags)
{
	struct stat	st;
	char *		slash;
	char		dpath[PATH_MAX];

	/* needed? */
	if (stat(path,&st) && (errno == ENOENT))
		if (lstat(path,&st) && (errno == ENOENT))
			return 0;

	/* output */
	*parg = path;

	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_uninstall(dctx,ectx))
			return SLBT_NESTED_ERROR(dctx);

	/* directory? */
	if (S_ISDIR(st.st_mode)) {
		if (!(rmdir(path)))
			return 0;

		else if ((errno == EEXIST) || (errno == ENOTEMPTY))
			return 0;

		else
			return SLBT_SYSTEM_ERROR(dctx);
	}

	/* remove file or symlink entry */
	if (unlink(path))
		return SLBT_SYSTEM_ERROR(dctx);

	/* remove empty containing directory? */
	if (flags & SLBT_UNINSTALL_RMDIR) {
		strcpy(dpath,path);

		/* invalid (current) directory? */
		if (!(slash = strrchr(dpath,'/')))
			return 0;

		*slash = 0;

		if (rmdir(dpath))
			return SLBT_SYSTEM_ERROR(dctx);
	}

	return 0;
}

static int slbt_exec_uninstall_versioned_library(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char **				parg,
	char *				rpath,
	char *				lpath,
	const char *			suffix,
	uint32_t			flags)
{
	char *	slash;
	char *	dot;
	char *	path;
	char	apath[PATH_MAX];

	/* normalize library link path */
	if (lpath[0] == '/') {
		path = lpath;

	} else if (!(slash = strrchr(rpath,'/'))) {
		path = lpath;

	} else {
		strcpy(apath,rpath);
		strcpy(&apath[slash-rpath+1],lpath);
		path = apath;
	}

	/* delete associated version files */
	while ((dot = strrchr(path,'.')) && (strcmp(dot,suffix))) {
		if (slbt_exec_uninstall_fs_entry(dctx,ectx,parg,path,flags))
			return SLBT_NESTED_ERROR(dctx);

		*dot = 0;
	}

	return 0;
}

static int slbt_exec_uninstall_entry(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	struct argv_entry *		entry,
	char **				parg,
	uint32_t			flags)
{
	char	path [PATH_MAX];
	char	lpath[PATH_MAX];
	char *	dot;

	if ((size_t)snprintf(path,PATH_MAX,"%s",
			entry->arg) >= PATH_MAX-8)
		return SLBT_BUFFER_ERROR(dctx);

	*parg = (char *)entry->arg;

	/* remove explicit argument */
	if (slbt_exec_uninstall_fs_entry(dctx,ectx,parg,path,flags))
		return SLBT_NESTED_ERROR(dctx);

	/* non-.la-wrapper argument? */
	if (!(dot = strrchr(path,'.')))
		return 0;

	else if (strcmp(dot,".la"))
		return 0;

	/* remove .a archive as needed */
	strcpy(dot,".a");

	if (slbt_exec_uninstall_fs_entry(dctx,ectx,parg,path,flags))
		return SLBT_NESTED_ERROR(dctx);

	/* .so symlink? */
	strcpy(dot,".so");

	if (!(slbt_readlink(path,lpath,sizeof(lpath))))
		if (slbt_exec_uninstall_versioned_library(
				dctx,ectx,parg,
				path,lpath,
				".so",flags))
			return SLBT_NESTED_ERROR(dctx);

	/* .lib.a symlink? */
	strcpy(dot,".lib.a");

	if (!(slbt_readlink(path,lpath,sizeof(lpath))))
		if (slbt_exec_uninstall_versioned_library(
				dctx,ectx,parg,
				path,lpath,
				".lib.a",flags))
			return SLBT_NESTED_ERROR(dctx);

	/* .dll symlink? */
	strcpy(dot,".dll");

	if (!(slbt_readlink(path,lpath,sizeof(lpath))))
		if (slbt_exec_uninstall_versioned_library(
				dctx,ectx,parg,
				path,lpath,
				".dll",flags))
			return SLBT_NESTED_ERROR(dctx);

	/* remove .so library as needed */
	strcpy(dot,".so");

	if (slbt_exec_uninstall_fs_entry(dctx,ectx,parg,path,flags))
		return SLBT_NESTED_ERROR(dctx);

	/* remove .lib.a import library as needed */
	strcpy(dot,".lib.a");

	if (slbt_exec_uninstall_fs_entry(dctx,ectx,parg,path,flags))
		return SLBT_NESTED_ERROR(dctx);

	/* remove .dll library as needed */
	strcpy(dot,".dll");

	if (slbt_exec_uninstall_fs_entry(dctx,ectx,parg,path,flags))
		return SLBT_NESTED_ERROR(dctx);

	/* remove .exe image as needed */
	strcpy(dot,".exe");

	if (slbt_exec_uninstall_fs_entry(dctx,ectx,parg,path,flags))
		return SLBT_NESTED_ERROR(dctx);

	/* remove binary image as needed */
	*dot = 0;

	if (slbt_exec_uninstall_fs_entry(dctx,ectx,parg,path,flags))
		return SLBT_NESTED_ERROR(dctx);

	return 0;
}

int slbt_exec_uninstall(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int				ret;
	char **				argv;
	char **				iargv;
	uint32_t			flags;
	struct slbt_exec_ctx *		actx;
	struct argv_meta *		meta;
	struct argv_entry *		entry;
	const struct argv_option *	optv[SLBT_OPTV_ELEMENTS];

	/* dry run */
	if (dctx->cctx->drvflags & SLBT_DRIVER_DRY_RUN)
		return 0;

	/* context */
	if (ectx)
		actx = 0;
	else if ((ret = slbt_get_exec_ctx(dctx,&ectx)))
		return ret;
	else
		actx = ectx;

	/* initial state, uninstall mode skin */
	slbt_reset_arguments(ectx);
	slbt_disable_placeholders(ectx);
	iargv = ectx->cargv;

	/* work around non-conforming uses of --mode=uninstall */
	if (!(strcmp(iargv[0],"/bin/sh")) || !strcmp(iargv[0],"/bin/bash"))
		iargv++;

	/* missing arguments? */
	argv_optv_init(slbt_uninstall_options,optv);

	if (!iargv[1] && (dctx->cctx->drvflags & SLBT_DRIVER_VERBOSITY_USAGE))
		return slbt_uninstall_usage(dctx->program,0,optv,0);

	/* <uninstall> argv meta */
	if (!(meta = argv_get(
			iargv,
			optv,
			dctx->cctx->drvflags & SLBT_DRIVER_VERBOSITY_ERRORS
				? ARGV_VERBOSITY_ERRORS
				: ARGV_VERBOSITY_NONE)))
		return slbt_exec_uninstall_fail(
			actx,meta,
			SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_UNINSTALL_FAIL));

	/* dest, alternate argument vector options */
	argv  = ectx->altv;
	flags = 0;

	*argv++ = iargv[0];

	for (entry=meta->entries; entry->fopt || entry->arg; entry++) {
		if (entry->fopt) {
			switch (entry->tag) {
				case TAG_UNINSTALL_HELP:
					flags |= SLBT_UNINSTALL_HELP;
					break;

				case TAG_UNINSTALL_VERSION:
					flags |= SLBT_UNINSTALL_VERSION;
					break;

				case TAG_UNINSTALL_FORCE:
					*argv++ = "-f";
					flags |= SLBT_UNINSTALL_FORCE;
					break;

				case TAG_UNINSTALL_RMDIR:
					*argv++ = "-d";
					flags |= SLBT_UNINSTALL_RMDIR;
					break;

				case TAG_UNINSTALL_VERBOSE:
					*argv++ = "-v";
					flags |= SLBT_UNINSTALL_VERBOSE;
					break;
			}
		}
	}

	/* --help */
	if (flags & SLBT_UNINSTALL_HELP) {
		slbt_uninstall_usage(dctx->program,0,optv,meta);
		return 0;
	}

	/* uninstall */
	ectx->argv    = ectx->altv;
	ectx->program = ectx->altv[0];

	/* uninstall entries one at a time */
	for (entry=meta->entries; entry->fopt || entry->arg; entry++)
		if (!entry->fopt)
			if (slbt_exec_uninstall_entry(dctx,ectx,entry,argv,flags))
				return slbt_exec_uninstall_fail(
					actx,meta,
					SLBT_NESTED_ERROR(dctx));

	argv_free(meta);
	slbt_free_exec_ctx(actx);

	return 0;
}
