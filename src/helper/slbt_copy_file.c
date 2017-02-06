/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2017  Z. Gilboa                            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <slibtool/slibtool.h>
#include "slibtool_spawn_impl.h"
#include "slibtool_errinfo_impl.h"

int slbt_copy_file(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				src,
	char *				dst)
{
	char **	oargv;
	char *	oprogram;
	char *	cp[4];
	int	ret;

	/* cp argv */
	cp[0] = "cp";
	cp[1] = src;
	cp[2] = dst;
	cp[3] = 0;

	/* alternate argument vector */
	oprogram      = ectx->program;
	oargv         = ectx->argv;
	ectx->argv    = cp;
	ectx->program = "cp";

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT)) {
		if (dctx->cctx->mode == SLBT_MODE_LINK) {
			if (slbt_output_link(dctx,ectx)) {
				ectx->argv = oargv;
				ectx->program = oprogram;
				return SLBT_NESTED_ERROR(dctx);
			}
		} else {
			if (slbt_output_install(dctx,ectx)) {
				ectx->argv = oargv;
				ectx->program = oprogram;
				return SLBT_NESTED_ERROR(dctx);
			}
		}
	}

	/* dlltool spawn */
	ret = ((slbt_spawn(ectx,true) < 0) || ectx->exitcode)
		? SLBT_SYSTEM_ERROR(dctx) : 0;

	ectx->argv = oargv;
	ectx->program = oprogram;
	return ret;
}
