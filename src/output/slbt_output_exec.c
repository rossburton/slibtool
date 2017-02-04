/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2017  Z. Gilboa                                  */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <slibtool/slibtool.h>
#include "slibtool_errinfo_impl.h"

static const char aclr_null[]    = "";
static const char aclr_reset[]   = "\x1b[0m";
static const char aclr_bold[]    = "\x1b[1m";

static const char aclr_green[]   = "\x1b[32m";
static const char aclr_blue[]    = "\x1b[34m";
static const char aclr_magenta[] = "\x1b[35m";

static int slbt_output_exec_annotated(
	const struct slbt_driver_ctx *	dctx,
	const struct slbt_exec_ctx *	ectx,
	const char *			step)
{
	char **      parg;
	const char * aclr_set;
	const char * aclr_color;
	const char * aclr_unset;

	if (fprintf(stdout,"%s%s%s: %s%s%s%s:%s",
			aclr_bold,aclr_magenta,
			dctx->program,aclr_reset,
			aclr_bold,aclr_green,step,aclr_reset) < 0)
		return SLBT_SYSTEM_ERROR(dctx);

	for (parg=ectx->argv; *parg; parg++) {
		if ((parg == ectx->lout[0]) || (parg == ectx->mout[0])) {
			aclr_set      = aclr_bold;
			aclr_color    = aclr_blue;
			aclr_unset    = aclr_null;
		} else {
			aclr_set      = aclr_null;
			aclr_color    = aclr_null;
			aclr_unset    = aclr_reset;
		}

		if (fprintf(stdout," %s%s%s%s",
				aclr_set,aclr_color,
				*parg,
				aclr_unset) < 0)
			return SLBT_SYSTEM_ERROR(dctx);

	}

	if (fputc('\n',stdout) < 0)
		return SLBT_SYSTEM_ERROR(dctx);

	return fflush(stdout);
}

static int slbt_output_exec_plain(
	const struct slbt_driver_ctx *	dctx,
	const struct slbt_exec_ctx *	ectx,
	const char *		step)
{
	char ** parg;

	if (fprintf(stdout,"%s: %s:",dctx->program,step) < 0)
		return SLBT_SYSTEM_ERROR(dctx);

	for (parg=ectx->argv; *parg; parg++)
		if (fprintf(stdout," %s",*parg) < 0)
			return SLBT_SYSTEM_ERROR(dctx);

	if (fputc('\n',stdout) < 0)
		return SLBT_SYSTEM_ERROR(dctx);

	return fflush(stdout)
		? SLBT_SYSTEM_ERROR(dctx)
		: 0;
}

int slbt_output_exec(
	const struct slbt_driver_ctx *	dctx,
	const struct slbt_exec_ctx *	ectx,
	const char *			step)
{
	if (dctx->cctx->drvflags & SLBT_DRIVER_ANNOTATE_NEVER)
		return slbt_output_exec_plain(dctx,ectx,step);

	else if (dctx->cctx->drvflags & SLBT_DRIVER_ANNOTATE_ALWAYS)
		return slbt_output_exec_annotated(dctx,ectx,step);

	else if (isatty(STDOUT_FILENO))
		return slbt_output_exec_annotated(dctx,ectx,step);

	else
		return slbt_output_exec_plain(dctx,ectx,step);
}

int slbt_output_compile(
	const struct slbt_driver_ctx *	dctx,
	const struct slbt_exec_ctx *	ectx)
{
	return slbt_output_exec(dctx,ectx,"compile");
}

int slbt_output_execute(
	const struct slbt_driver_ctx *	dctx,
	const struct slbt_exec_ctx *	ectx)
{
	return slbt_output_exec(dctx,ectx,"execute");
}

int slbt_output_install(
	const struct slbt_driver_ctx *	dctx,
	const struct slbt_exec_ctx *	ectx)
{
	return slbt_output_exec(dctx,ectx,"install");
}

int slbt_output_link(
	const struct slbt_driver_ctx *	dctx,
	const struct slbt_exec_ctx *	ectx)
{
	return slbt_output_exec(dctx,ectx,"link");
}

int slbt_output_uninstall(
	const struct slbt_driver_ctx *	dctx,
	const struct slbt_exec_ctx *	ectx)
{
	return slbt_output_exec(dctx,ectx,"uninstall");
}
