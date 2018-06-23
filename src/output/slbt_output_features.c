/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2017  Z. Gilboa                            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <slibtool/slibtool.h>
#include "slibtool_errinfo_impl.h"

static const char enable[]  = "enable";
static const char disable[] = "disable";

int slbt_output_features(const struct slbt_driver_ctx * dctx)
{
	const char * shared_option;
	const char * static_option;

	shared_option = (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_SHARED)
		? disable : enable;

	static_option = (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_STATIC)
		? disable : enable;

	if (fprintf(stdout,"host: %s\n",dctx->cctx->host.host) < 0)
		return SLBT_SYSTEM_ERROR(dctx);

	if (fprintf(stdout,"%s shared libraries\n",shared_option) < 0)
		return SLBT_SYSTEM_ERROR(dctx);

	if (fprintf(stdout,"%s static libraries\n",static_option) < 0)
		return SLBT_SYSTEM_ERROR(dctx);

	return fflush(stdout)
		? SLBT_SYSTEM_ERROR(dctx)
		: 0;
}
