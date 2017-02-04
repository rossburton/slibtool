/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2017  Z. Gilboa                                  */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

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

static int slbt_exec_compile_remove_file(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			target)
{
	(void)ectx;

	/* remove target (if any) */
	if (!(unlink(target)) || (errno == ENOENT))
		return 0;

	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		strerror(errno);

	return SLBT_SYSTEM_ERROR(dctx);
}

int  slbt_exec_compile(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int			ret;
	struct slbt_exec_ctx *	actx = 0;

	/* dry run */
	if (dctx->cctx->drvflags & SLBT_DRIVER_DRY_RUN)
		return 0;

	/* context */
	if (ectx)
		slbt_reset_placeholders(ectx);
	else if ((ret = slbt_get_exec_ctx(dctx,&ectx)))
		return ret;
	else
		actx = ectx;

	/* remove old .lo wrapper */
	if (slbt_exec_compile_remove_file(dctx,ectx,ectx->ltobjname))
		return SLBT_NESTED_ERROR(dctx);

	/* .libs directory */
	if (dctx->cctx->drvflags & SLBT_DRIVER_SHARED)
		if (slbt_mkdir(ectx->ldirname)) {
			slbt_free_exec_ctx(actx);
			return SLBT_SYSTEM_ERROR(dctx);
		}

	/* compile mode */
	ectx->program = ectx->compiler;
	ectx->argv    = ectx->cargv;

	/* shared library object */
	if (dctx->cctx->drvflags & SLBT_DRIVER_SHARED) {
		if (!(dctx->cctx->drvflags & SLBT_DRIVER_ANTI_PIC)) {
			*ectx->dpic = "-DPIC";
			*ectx->fpic = "-fPIC";
		}

		switch (dctx->cctx->tag) {
			case SLBT_TAG_NASM:
				break;

			default:
				*ectx->cass = "-c";
				break;
		}

		*ectx->lout[0] = "-o";
		*ectx->lout[1] = ectx->lobjname;

		if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT)) {
			if (slbt_output_compile(dctx,ectx)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}
		}

		if (((ret = slbt_spawn(ectx,true)) < 0) || ectx->exitcode) {
			slbt_free_exec_ctx(actx);
			return SLBT_SYSTEM_ERROR(dctx);
		}
	}

	/* static archive object */
	if (dctx->cctx->drvflags & SLBT_DRIVER_STATIC) {
		slbt_reset_placeholders(ectx);

		if (dctx->cctx->drvflags & SLBT_DRIVER_PRO_PIC) {
			*ectx->dpic = "-DPIC";
			*ectx->fpic = "-fPIC";
		}

		switch (dctx->cctx->tag) {
			case SLBT_TAG_NASM:
				break;

			default:
				*ectx->cass = "-c";
				break;
		}

		*ectx->lout[0] = "-o";
		*ectx->lout[1] = ectx->aobjname;

		if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT)) {
			if (slbt_output_compile(dctx,ectx)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}
		}

		if (((ret = slbt_spawn(ectx,true)) < 0) || ectx->exitcode) {
			slbt_free_exec_ctx(actx);
			return SLBT_SYSTEM_ERROR(dctx);
		}
	}

	ret = slbt_create_object_wrapper(dctx,ectx);
	slbt_free_exec_ctx(actx);

	return ret ? SLBT_NESTED_ERROR(dctx) : 0;
}
