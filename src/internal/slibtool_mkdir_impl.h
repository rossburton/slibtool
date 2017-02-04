/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2017  Z. Gilboa                                  */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <errno.h>
#include <unistd.h>

#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

static inline int slbt_mkdir(const char * path)
{
	int fdlibs;

	if ((fdlibs = open(path,O_DIRECTORY)) >= 0)
		close(fdlibs);
	else if ((errno != ENOENT) || mkdir(path,0777))
		if (errno != EEXIST)
			return -1;

	return 0;
}
