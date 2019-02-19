/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2018  Z. Gilboa                            */
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

	return SLBT_SYSTEM_ERROR(dctx);
}

static int slbt_exec_compile_finalize_argument_vector(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	char *		sargv[1024];
	char **		sargvbuf;
	char **		base;
	char **		parg;
	char **		aarg;
	char **		aargv;
	char **		cap;
	char **		src;
	char **		dst;
	char *		ccwrap;

	/* vector size */
	base = ectx->argv;
	parg = ectx->argv;

	for (; *parg; )
		parg++;

	/* buffer */
	if (parg - base < 1024) {
		aargv    = sargv;
		aarg     = aargv;
		sargvbuf = 0;

	} else if (!(sargvbuf = calloc(parg-base+1,sizeof(char *)))) {
		return SLBT_SYSTEM_ERROR(dctx);

	} else {
		aargv = sargvbuf;
		aarg  = aargv;
	}

	/* (program name) */
	parg = &base[1];

	/* split object args from all other args, record output */
	/* annotation, and remove redundant -l arguments       */
	for (; *parg; ) {
		if (ectx->lout[0] == parg) {
			ectx->lout[0] = &aarg[0];
			ectx->lout[1] = &aarg[1];
		}

		if (ectx->mout[0] == parg) {
			ectx->mout[0] = &aarg[0];
			ectx->mout[1] = &aarg[1];
		}

		/* placeholder argument? */
		if (!strncmp(*parg,"-USLIBTOOL_PLACEHOLDER_",23)) {
			parg++;
		} else {
			*aarg++ = *parg++;
		}
	}

	/* program name, ccwrap */
	if ((ccwrap = (char *)dctx->cctx->ccwrap)) {
		base[1] = base[0];
		base[0] = ccwrap;
		base++;
	}

	/* join all other args */
	src = aargv;
	cap = aarg;
	dst = &base[1];

	for (; src<cap; )
		*dst++ = *src++;

	/* properly null-terminate argv, accounting for redundant arguments */
	*dst = 0;

	/* output annotation */
	if (ectx->lout[0]) {
		ectx->lout[0] = &base[1] + (ectx->lout[0] - aargv);
		ectx->lout[1] = ectx->lout[0] + 1;
	}

	if (ectx->mout[0]) {
		ectx->mout[0] = &base[1] + (ectx->mout[0] - aargv);
		ectx->mout[1] = ectx->mout[0] + 1;
	}

	/* all done */
	if (sargvbuf)
		free(sargvbuf);

	return 0;
}

int  slbt_exec_compile(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int				ret;
	char *				fpic;
	char *				ccwrap;
	struct slbt_exec_ctx *		actx = 0;
	const struct slbt_common_ctx *	cctx = dctx->cctx;

	/* dry run */
	if (cctx->drvflags & SLBT_DRIVER_DRY_RUN)
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
	if (cctx->drvflags & SLBT_DRIVER_SHARED)
		if (slbt_mkdir(dctx,ectx->ldirname)) {
			slbt_free_exec_ctx(actx);
			return SLBT_SYSTEM_ERROR(dctx);
		}

	/* compile mode */
	ccwrap        = (char *)cctx->ccwrap;
	ectx->program = ccwrap ? ccwrap : ectx->compiler;
	ectx->argv    = ectx->cargv;

	/* -fpic */
	fpic = *ectx->fpic;

	if ((cctx->tag == SLBT_TAG_CC) || (cctx->tag == SLBT_TAG_CXX))
		if (cctx->settings.picswitch)
			fpic = cctx->settings.picswitch;

	/* shared library object */
	if (cctx->drvflags & SLBT_DRIVER_SHARED) {
		if (!(cctx->drvflags & SLBT_DRIVER_ANTI_PIC)) {
			*ectx->dpic = "-DPIC";
			*ectx->fpic = fpic;
		}

		*ectx->lout[0] = "-o";
		*ectx->lout[1] = ectx->lobjname;

		if (slbt_exec_compile_finalize_argument_vector(dctx,ectx))
			return SLBT_NESTED_ERROR(dctx);

		if (!(cctx->drvflags & SLBT_DRIVER_SILENT)) {
			if (slbt_output_compile(dctx,ectx)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}
		}

		if (((ret = slbt_spawn(ectx,true)) < 0) || ectx->exitcode) {
			slbt_free_exec_ctx(actx);
			return SLBT_SYSTEM_ERROR(dctx);
		}

		if (cctx->drvflags & SLBT_DRIVER_STATIC)
			slbt_reset_argvector(ectx);
	}

	/* static archive object */
	if (cctx->drvflags & SLBT_DRIVER_STATIC) {
		slbt_reset_placeholders(ectx);

		if (cctx->drvflags & SLBT_DRIVER_PRO_PIC) {
			*ectx->dpic = "-DPIC";
			*ectx->fpic = fpic;
		}

		*ectx->lout[0] = "-o";
		*ectx->lout[1] = ectx->aobjname;

		if (slbt_exec_compile_finalize_argument_vector(dctx,ectx))
			return SLBT_NESTED_ERROR(dctx);

		if (!(cctx->drvflags & SLBT_DRIVER_SILENT)) {
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
