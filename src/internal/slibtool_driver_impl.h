#ifndef SOFORT_DRIVER_IMPL_H
#define SOFORT_DRIVER_IMPL_H

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include <slibtool/slibtool.h>
#include "argv/argv.h"

extern const struct argv_option slbt_default_options[];

enum app_tags {
	TAG_HELP,
	TAG_HELP_ALL,
	TAG_VERSION,
	TAG_CONFIG,
	TAG_DEBUG,
	TAG_DRY_RUN,
	TAG_FEATURES,
	TAG_LEGABITS,
	TAG_MODE,
	TAG_FINISH,
	TAG_WARNINGS,
	TAG_ANNOTATE,
	TAG_DEPS,
	TAG_SILENT,
	TAG_TAG,
	TAG_VERBOSE,
	TAG_TARGET,
	TAG_HOST,
	TAG_FLAVOR,
	TAG_AR,
	TAG_RANLIB,
	TAG_DLLTOOL,
	TAG_OUTPUT,
	TAG_BINDIR,
	TAG_LDRPATH,
	TAG_SHREXT,
	TAG_RPATH,
	TAG_RELEASE,
	TAG_DLOPEN,
	TAG_EXPSYM_FILE,
	TAG_EXPSYM_REGEX,
	TAG_VERSION_INFO,
	TAG_VERSION_NUMBER,
	TAG_NO_SUPPRESS,
	TAG_NO_INSTALL,
	TAG_PREFER_PIC,
	TAG_PREFER_NON_PIC,
	TAG_SHARED,
	TAG_STATIC,
	TAG_ALL_STATIC,
	TAG_DISABLE_STATIC,
	TAG_DISABLE_SHARED,
	TAG_NO_UNDEFINED,
	TAG_MODULE,
	TAG_AVOID_VERSION,
	TAG_COMPILER_FLAG,
	TAG_VERBATIM_FLAG,
};

struct slbt_host_strs {
	char *		machine;
	char *		host;
	char *		flavor;
	char *		ar;
	char *		ranlib;
	char *		dlltool;
};

struct slbt_driver_ctx_impl {
	struct slbt_common_ctx	cctx;
	struct slbt_driver_ctx	ctx;
	struct slbt_host_strs	host;
	struct slbt_host_strs	ahost;
	char *			libname;
	char **			targv;
	char **			cargv;
	struct slbt_error_info**errinfp;
	struct slbt_error_info**erricap;
	struct slbt_error_info *erriptr[64];
	struct slbt_error_info	erribuf[64];
};

struct slbt_unit_ctx_impl {
	const char *		path;
	struct slbt_input	map;
	struct slbt_common_ctx	cctx;
	struct slbt_unit_ctx	uctx;
};

static inline struct slbt_driver_ctx_impl * slbt_get_driver_ictx(const struct slbt_driver_ctx * dctx)
{
	uintptr_t addr;

	if (dctx) {
		addr = (uintptr_t)dctx - offsetof(struct slbt_driver_ctx_impl,ctx);
		return (struct slbt_driver_ctx_impl *)addr;
	}

	return 0;
}

#endif
