/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2018  Z. Gilboa                            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_errinfo_impl.h"

#ifndef SLBT_TAB_WIDTH
#define SLBT_TAB_WIDTH 8
#endif

#ifndef SLBT_KEY_WIDTH
#define SLBT_KEY_WIDTH 16
#endif

static bool slbt_output_config_line(
	int		fd,
	const char *	key,
	const char *	value,
	const char *	annotation,
	int		midwidth)
{
	return (slbt_dprintf(fd,"%-*s%-*s%s\n",
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
	int				fdout;

	cctx     = dctx->cctx;
	compiler = cctx->cargv[0] ? cctx->cargv[0] : "";
	target   = cctx->target   ? cctx->target   : "";
	midwidth = strlen(compiler);
	fdout    = slbt_driver_fdout(dctx);

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

	if ((len = strlen(cctx->host.windres)) > midwidth)
		midwidth = len;

	if ((len = strlen(cctx->host.dlltool)) > midwidth)
		midwidth = len;

	if ((len = strlen(cctx->host.mdso)) > midwidth)
		midwidth = len;

	midwidth += SLBT_TAB_WIDTH;
	midwidth &= (~(SLBT_TAB_WIDTH-1));

	if (slbt_output_config_line(fdout,"key","value","annotation",midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line(fdout,"---","-----","----------",midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line(fdout,"compiler",cctx->cargv[0],"",midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line(fdout,"target",cctx->target,"",midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line(fdout,"host",cctx->host.host,cctx->cfgmeta.host,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line(fdout,"flavor",cctx->host.flavor,cctx->cfgmeta.flavor,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line(fdout,"ar",cctx->host.ar,cctx->cfgmeta.ar,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line(fdout,"ranlib",cctx->host.ranlib,cctx->cfgmeta.ranlib,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line(fdout,"windres",cctx->host.windres,cctx->cfgmeta.windres,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line(fdout,"dlltool",cctx->host.dlltool,cctx->cfgmeta.dlltool,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	if (slbt_output_config_line(fdout,"mdso",cctx->host.mdso,cctx->cfgmeta.mdso,midwidth))
		return SLBT_SYSTEM_ERROR(dctx);

	return 0;
}
