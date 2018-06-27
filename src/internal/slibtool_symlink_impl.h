#ifndef SLIBTOOL_SYMLINK_IMPL_H
#define SLIBTOOL_SYMLINK_IMPL_H

#include <stdbool.h>
#include <slibtool/slibtool.h>

int slbt_create_symlink(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			target,
	const char *			lnkname,
	bool				flawrapper);

int slbt_symlink_is_a_placeholder(char * lnkpath);

#endif
