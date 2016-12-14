/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016  Z. Gilboa                                  */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "slibtool_errinfo_impl.h"
#include "slibtool_symlink_impl.h"

#define SLBT_DEV_NULL_FLAGS	(SLBT_DRIVER_ALL_STATIC      \
				| SLBT_DRIVER_DISABLE_SHARED \
				| SLBT_DRIVER_DISABLE_STATIC)

int slbt_create_symlink(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			target,
	const char *			lnkname,
	bool				flawrapper)
{
	char **		oargv;
	const char *	slash;
	char *		ln[5];
	char *		dotdot;
	char		tmplnk [PATH_MAX];
	char		lnkarg [PATH_MAX];
	char		atarget[PATH_MAX];

	/* atarget */
	if ((dctx->cctx->drvflags & SLBT_DEV_NULL_FLAGS)
			&& !strcmp(target,"/dev/null"))
		slash = target;
	else if ((slash = strrchr(target,'/')))
		slash++;
	else
		slash = target;

	dotdot = flawrapper ? "../" : "";

	if ((size_t)snprintf(atarget,sizeof(atarget),"%s%s",
			dotdot,slash) >= sizeof(atarget))
		return SLBT_BUFFER_ERROR(dctx);

	/* tmplnk */
	if ((size_t)snprintf(tmplnk,sizeof(tmplnk),"%s.symlink.tmp",
			lnkname) >= sizeof(tmplnk))
		return SLBT_BUFFER_ERROR(dctx);

	/* lnkarg */
	strcpy(lnkarg,lnkname);

	/* ln argv (fake) */
	ln[0] = "ln";
	ln[1] = "-s";
	ln[2] = atarget;
	ln[3] = lnkarg;
	ln[4] = 0;

	oargv      = ectx->argv;
	ectx->argv = ln;

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT)) {
		if (dctx->cctx->mode == SLBT_MODE_LINK) {
			if (slbt_output_link(dctx,ectx)) {
				ectx->argv = oargv;
				return SLBT_NESTED_ERROR(dctx);
			}
		} else {
			if (slbt_output_install(dctx,ectx)) {
				ectx->argv = oargv;
				return SLBT_NESTED_ERROR(dctx);
			}
		}
	}

	/* restore execution context */
	ectx->argv = oargv;

	/* create symlink */
	if (symlink(atarget,tmplnk))
		return SLBT_SYSTEM_ERROR(dctx);

	return rename(tmplnk,lnkname)
		? SLBT_SYSTEM_ERROR(dctx)
		: 0;
}
