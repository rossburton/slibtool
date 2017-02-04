/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2017  Z. Gilboa                                  */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <slibtool/slibtool.h>
#include "slibtool_errinfo_impl.h"

#ifndef SLBT_TAB_WIDTH
#define SLBT_TAB_WIDTH 8
#endif

#ifndef SLBT_KEY_WIDTH
#define SLBT_KEY_WIDTH 16
#endif

static bool slbt_output_config_line(
	const char *	key,
	const char *	value,
	const char *	annotation,
	int		midwidth)
{
	return (fprintf(stdout,"%-*s%-*s%s\n",
			SLBT_KEY_WIDTH, key,
			midwidth, value ? value : "",
			annotation ? annotation : "") < 0)
		? true : false;
}

int slbt_output_config(const struct slbt_driver_ctx * dctx)
{
	const struct slbt_common_ctx *	cctx;
	const char *			compiler;
	const char *			target;
	int				len;
	int				midwidth;

	cctx     = dctx->cctx;
	compiler = cctx->cargv[0] ? cctx->cargv[0] : "";
	target   = cctx->target   ? cctx->target   : "";
	midwidth = strlen(compiler);

	if ((len = strlen(target)) > midwidth)
		midwidth = len;

	if ((len = strlen(cctx->host.host)) > midwidth)
		midwidth = len;

	if ((len = strlen(cctx->host.flavor)) > midwidth)
		midwidth = len;

	if ((len = strlen(cctx->host.ar)) > midwidth)
		midwidth = len;

	if ((len = strlen(cctx->host.ranlib)) > midwidth)
		midwidth = len;

	if ((len = strlen(cctx->host.dlltool)) > midwidth)
		midwidth = len;

	midwidth += SLBT_TAB_WIDTH;
	midwidth &= (~(SLBT_TAB_WIDTH-1));

	if (slbt_output_config_line("key","value","annotation",midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line("---","-----","----------",midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line("compiler",cctx->cargv[0],"",midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line("target",cctx->target,"",midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line("host",cctx->host.host,cctx->cfgmeta.host,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line("flavor",cctx->host.flavor,cctx->cfgmeta.flavor,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line("ar",cctx->host.ar,cctx->cfgmeta.ar,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line("ranlib",cctx->host.ranlib,cctx->cfgmeta.ranlib,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line("dlltool",cctx->host.dlltool,cctx->cfgmeta.dlltool,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	return fflush(stdout)
		? SLBT_SYSTEM_ERROR(dctx)
		: 0;
}
