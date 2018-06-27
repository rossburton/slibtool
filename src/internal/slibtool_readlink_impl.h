/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2018  Z. Gilboa                            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#ifndef SLIBTOOL_READLINK_IMPL_H
#define SLIBTOOL_READLINK_IMPL_H

#include <unistd.h>

static inline int slbt_readlink(
	const char *	restrict path,
	char *		restrict buf,
	ssize_t		bufsize)
{
	ssize_t ret;

	if ((ret = readlink(path,buf,bufsize)) <= 0)
		return -1;
	else if (ret == bufsize)
		return -1;
	else {
		buf[ret] = 0;
		return 0;
	}
}

#endif
