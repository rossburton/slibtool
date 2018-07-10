/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2018  Z. Gilboa                            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"

#define SLBT_ARGV_SPARE_PTRS	16

static size_t slbt_parse_comma_separated_flags(
	const char *	str,
	int *		argc)
{
	const char * ch;

	for (ch=str; *ch; ch++)
		if (*ch == ',')
			(*argc)++;

	return ch - str;
}


static char * slbt_source_file(char ** argv)
{
	char **	parg;
	char *	ch;

	for (parg=argv; *parg; parg++)
		if ((ch = strrchr(*parg,'.')))
			if ((!(strcmp(++ch,"s")))
					|| (!(strcmp(ch,"S")))
					|| (!(strcmp(ch,"asm")))
					|| (!(strcmp(ch,"c")))
					|| (!(strcmp(ch,"cc")))
					|| (!(strcmp(ch,"cpp")))
					|| (!(strcmp(ch,"cxx"))))
				return *parg;
	return 0;
}


static struct slbt_exec_ctx_impl * slbt_exec_ctx_alloc(
	const struct slbt_driver_ctx *	dctx)
{
	struct slbt_exec_ctx_impl *	ictx;
	size_t				size;
	size_t				vsize;
	int				argc;
	char *				args;
	char *				shadow;
	char *				csrc;
	char **				parg;

	argc = 0;
	csrc = 0;

	/* clerical [worst-case] buffer size (guard, .libs, version) */
	size  = strlen(".lo") + 1;
	size += 12 * (strlen(".libs/") + 1);
	size += 36 * (strlen(".0000") + 1);

	/* buffer size (cargv, -Wc) */
	for (parg=dctx->cctx->cargv; *parg; parg++, argc++)
		if (!(strncmp("-Wc,",*parg,4)))
			size += slbt_parse_comma_separated_flags(
				&(*parg)[4],&argc) + 1;
		else
			size += strlen(*parg) + 1;

	/* buffer size (ldirname, lbasename, lobjname, aobjname, etc.) */
	if (dctx->cctx->output)
		size += 8*strlen(dctx->cctx->output);
	else if ((csrc = slbt_source_file(dctx->cctx->cargv)))
		size += 8*strlen(csrc);

	/* pessimistic argc: .libs/libfoo.so --> -L.libs -lfoo */
	argc *= 2;
	argc += SLBT_ARGV_SPARE_PTRS;

	/* buffer size (.libs/%.o, pessimistic) */
	size += argc * strlen(".libs/-L-l");

	/* buffer size (linking) */
	if (dctx->cctx->mode == SLBT_MODE_LINK)
		size += strlen(dctx->cctx->settings.arprefix) + 1
			+ strlen(dctx->cctx->settings.arsuffix) + 1
			+ strlen(dctx->cctx->settings.dsoprefix) + 1
			+ strlen(dctx->cctx->settings.dsoprefix) + 1
			+ strlen(dctx->cctx->settings.dsoprefix) + 1
			+ strlen(dctx->cctx->settings.exeprefix) + 1
			+ strlen(dctx->cctx->settings.exeprefix) + 1
			+ strlen(dctx->cctx->settings.impprefix) + 1
			+ strlen(dctx->cctx->settings.impprefix) + 1;

	/* alloc */
	if (!(args = malloc(size)))
		return 0;

	if (!(shadow = malloc(size))) {
		free(args);
		return 0;
	}

	/* ictx, argv, xargv */
	vsize  = sizeof(*ictx);
	vsize += sizeof(char *) * (argc + 1);
	vsize += sizeof(char *) * (argc + 1);

	/* altv: duplicate set, -Wl,--whole-archive, -Wl,--no-whole-archive */
	vsize += sizeof(char *) * (argc + 1) * 3;

	if (!(ictx = calloc(1,vsize))) {
		free(args);
		free(shadow);
		return 0;
	}

	ictx->args = args;
	ictx->argc = argc;

	ictx->size   = size;
	ictx->shadow = shadow;

	ictx->ctx.csrc  = csrc;
	ictx->fdwrapper = (-1);

	return ictx;
}


int  slbt_get_exec_ctx(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx **		ectx)
{
	struct slbt_exec_ctx_impl *	ictx;
	char **				parg;
	char *				ch;
	char *				mark;
	const char *			dmark;
	char *				slash;
	const char *			arprefix;
	const char *			dsoprefix;
	const char *			impprefix;
	const char *			ref;
	int				i;

	/* alloc */
	if (!(ictx = slbt_exec_ctx_alloc(dctx)))
		return SLBT_NESTED_ERROR(dctx);

	/* init with guard for later .lo check */
	ch                = ictx->args + strlen(".lo");
	ictx->ctx.argv    = ictx->vbuffer;
	ictx->ctx.xargv   = &ictx->ctx.argv [ictx->argc + 1];
	ictx->ctx.altv    = &ictx->ctx.xargv[ictx->argc + 1];

	/* <compiler> */
	ictx->ctx.compiler = dctx->cctx->cargv[0];
	ictx->ctx.cargv    = ictx->ctx.argv;

	/* ldirname, lbasename */
	ref = (dctx->cctx->output)
		? dctx->cctx->output
		: ictx->ctx.csrc;

	if (ref && !ictx->ctx.csrc && (mark = strrchr(ref,'/'))) {
		if (!(strncmp(ref,"../",3)))
			dmark = 0;
		else if (!(strncmp(ref,"./",2)))
			dmark = &ref[1];
		else
			dmark = strchr(ref,'/');

		for (; dmark; ) {
			if (!(strncmp(dmark,"/./",3))) {
				dmark = strchr(&dmark[2],'/');
			} else if (!(strncmp(dmark,"/../",4))) {
				ictx->ctx.ldirdepth = -1;
				dmark = 0;
			} else {
				for (; *dmark=='/'; )
					dmark++;

				ictx->ctx.ldirdepth++;
				dmark = strchr(dmark,'/');
			}
		}

		ictx->ctx.ldirname = ch;
		strcpy(ch,ref);
		ch += mark - ref;
		ch += sprintf(ch,"%s","/.libs/");
		ch++;

		ictx->ctx.lbasename = ch;
		ch += sprintf(ch,"%s",++mark);
		ch++;
	} else if (ref) {
		ictx->ctx.ldirname = ch;
		ch += sprintf(ch,"%s",".libs/");
		ch++;

		ictx->ctx.lbasename = ch;
		mark = strrchr(ref,'/');
		ch += sprintf(ch,"%s",mark ? ++mark : ref);
		ch++;
	}

	/* lbasename suffix */
	if (ref && (dctx->cctx->mode == SLBT_MODE_COMPILE)) {
		if ((ch[-4] == '.') && (ch[-3] == 'l') && (ch[-2] == 'o')) {
			ch[-3] = 'o';
			ch[-2] = 0;
			ch--;
		} else if (ictx->ctx.csrc) {
			if ((mark = strrchr(ictx->ctx.lbasename,'.'))) {
				ch    = mark;
				*++ch = 'o';
				*++ch = 0;
				ch++;
			}
		}
	}

	/* cargv, -Wc */
	for (i=0, parg=dctx->cctx->cargv; *parg; parg++, ch++) {
		if (!(strncmp("-Wc,",*parg,4))) {
			strcpy(ch,&(*parg)[4]);
			ictx->ctx.argv[i++] = ch;

			for (; *ch; ch++)
				if (*ch == ',') {
					*ch++ = 0;
					ictx->ctx.argv[i++] = ch;
				}
		} else {
			ictx->ctx.argv[i++] = ch;
			ch += sprintf(ch,"%s",*parg);
			ch += strlen(".libs/-L-l");
		}
	}

	/* placeholders for -DPIC, -fPIC, -c, -o, <output> */
	ictx->ctx.dpic = &ictx->ctx.argv[i++];
	ictx->ctx.fpic = &ictx->ctx.argv[i++];
	ictx->ctx.cass = &ictx->ctx.argv[i++];

	ictx->ctx.noundef = &ictx->ctx.argv[i++];
	ictx->ctx.soname  = &ictx->ctx.argv[i++];
	ictx->ctx.lsoname = &ictx->ctx.argv[i++];
	ictx->ctx.symdefs = &ictx->ctx.argv[i++];
	ictx->ctx.symfile = &ictx->ctx.argv[i++];

	ictx->ctx.lout[0] = &ictx->ctx.argv[i++];
	ictx->ctx.lout[1] = &ictx->ctx.argv[i++];

	ictx->ctx.rpath[0] = &ictx->ctx.argv[i++];
	ictx->ctx.rpath[1] = &ictx->ctx.argv[i++];

	ictx->ctx.sentinel= &ictx->ctx.argv[i++];

	slbt_reset_placeholders(&ictx->ctx);

	/* dsoprefix */
	if (dctx->cctx->settings.dsoprefix) {
		ictx->dsoprefix = ch;
		strcpy(ch,dctx->cctx->settings.dsoprefix);
		ch += strlen(ch) + 1;
	}

	/* output file name */
	if (ref && ((dctx->cctx->mode == SLBT_MODE_COMPILE))) {
		*ictx->ctx.lout[0] = "-o";
		*ictx->ctx.lout[1] = ch;
		ictx->ctx.lobjname = ch;

		ch += sprintf(ch,"%s%s",
			ictx->ctx.ldirname,
			ictx->ctx.lbasename)
			+ 1;

		ictx->ctx.aobjname = ch;

		ch += sprintf(ch,"%s",ictx->ctx.ldirname);
		ch -= strlen(".libs/");
		ch += sprintf(ch,"%s",
			ictx->ctx.lbasename)
			+ 1;

		ictx->ctx.ltobjname = ch;
		strcpy(ch,ictx->ctx.aobjname);

		if ((mark = strrchr(ch,'.')))
			ch = mark + sprintf(mark,"%s",".lo")
				+ 1;
	}

	/* linking: arfilename, lafilename, laifilename, dsofilename */
	if (dctx->cctx->mode == SLBT_MODE_LINK && dctx->cctx->libname) {
		/* arprefix, dsoprefix */
		if (dctx->cctx->drvflags & SLBT_DRIVER_MODULE) {
			ictx->ctx.sonameprefix = "";
			arprefix  = "";
			dsoprefix = "";
			impprefix = "";
		} else {
			ictx->ctx.sonameprefix = ictx->dsoprefix;
			arprefix  = dctx->cctx->settings.arprefix;
			dsoprefix = dctx->cctx->settings.dsoprefix;
			impprefix = dctx->cctx->settings.impprefix;
		}

		/* arfilename */
		ictx->ctx.arfilename = ch;
		ch += sprintf(ch,"%s%s%s%s",
				ictx->ctx.ldirname,
				arprefix,
				dctx->cctx->libname,
				dctx->cctx->settings.arsuffix);
		ch++;



		/* lafilename */
		ictx->ctx.lafilename = ch;
		ch += sprintf(ch,"%s%s%s.la",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname);
		ch++;


		/* laifilename */
		ictx->ctx.laifilename = ch;
		ch += sprintf(ch,"%s%s%s.lai",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname);
		ch++;


		/* dsofilename */
		ictx->ctx.dsofilename = ch;
		ch += sprintf(ch,"%s%s%s%s",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname,
				dctx->cctx->settings.dsosuffix);
		ch++;

		/* deffilename */
		ictx->ctx.deffilename = ch;
		ch += sprintf(ch,"%s%s%s%s.def",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname,
				dctx->cctx->settings.dsosuffix);
		ch++;

		/* rpathfilename */
		ictx->ctx.rpathfilename = ch;
		ch += sprintf(ch,"%s%s%s%s.slibtool.rpath",
				ictx->ctx.ldirname,
				dsoprefix,
				dctx->cctx->libname,
				dctx->cctx->settings.dsosuffix);
		ch++;

		/* default implib file name */
		ictx->ctx.dimpfilename = ch;
		ch += sprintf(ch,"%s%s%s%s",
				ictx->ctx.ldirname,
				impprefix,
				dctx->cctx->libname,
				dctx->cctx->settings.impsuffix);
		ch++;


		/* primary implib file name */
		ictx->ctx.pimpfilename = ch;
		ch += sprintf(ch,"%s%s%s.%d%s",
				ictx->ctx.ldirname,
				impprefix,
				dctx->cctx->libname,
				dctx->cctx->verinfo.major,
				dctx->cctx->settings.impsuffix);
		ch++;

		/* versioned implib file name */
		ictx->ctx.vimpfilename = ch;
		ch += sprintf(ch,"%s%s%s.%d.%d.%d%s",
				ictx->ctx.ldirname,
				impprefix,
				dctx->cctx->libname,
				dctx->cctx->verinfo.major,
				dctx->cctx->verinfo.minor,
				dctx->cctx->verinfo.revision,
				dctx->cctx->settings.impsuffix);
		ch++;

		/* relfilename */
		if (dctx->cctx->release) {
			ictx->ctx.relfilename = ch;
			ch += dctx->cctx->verinfo.verinfo
				? sprintf(ch,"%s%s%s-%s%s.%d.%d.%d",
					ictx->ctx.ldirname,
					dsoprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dctx->cctx->settings.dsosuffix,
					dctx->cctx->verinfo.major,
					dctx->cctx->verinfo.minor,
					dctx->cctx->verinfo.revision)
				: sprintf(ch,"%s%s%s-%s%s",
					ictx->ctx.ldirname,
					dsoprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dctx->cctx->settings.dsosuffix);
			ch++;
		}

		/* dsorellnkname */
		if (dctx->cctx->release) {
			ictx->ctx.dsorellnkname = ch;
			ch += sprintf(ch,"%s%s%s-%s%s",
					ictx->ctx.ldirname,
					dsoprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dctx->cctx->settings.dsosuffix);
			ch++;
		}
	}

	/* linking: exefilename */
	if (dctx->cctx->mode == SLBT_MODE_LINK && !dctx->cctx->libname) {
		ictx->ctx.exefilename = ch;

		if ((slash = strrchr(dctx->cctx->output,'/'))) {
			strcpy(ch,dctx->cctx->output);
			mark = ch + (slash - dctx->cctx->output);
			sprintf(++mark,".libs/%s",++slash);
			ch += strlen(ch) + 1;
		} else
			ch += sprintf(ch,".libs/%s",dctx->cctx->output) + 1;
	}

	/* argument strings shadow copy */
	memcpy(ictx->shadow,ictx->args,ictx->size);

	*ectx = &ictx->ctx;
	return 0;
}


static int slbt_free_exec_ctx_impl(
	struct slbt_exec_ctx_impl *	ictx,
	int				status)
{
	if (ictx->fdwrapper >= 0)
		close(ictx->fdwrapper);

	free(ictx->args);
	free(ictx->shadow);
	free (ictx);

	return status;
}


void slbt_free_exec_ctx(struct slbt_exec_ctx * ctx)
{
	struct slbt_exec_ctx_impl *	ictx;
	uintptr_t			addr;

	if (ctx) {
		addr = (uintptr_t)ctx - offsetof(struct slbt_exec_ctx_impl,ctx);
		ictx = (struct slbt_exec_ctx_impl *)addr;
		slbt_free_exec_ctx_impl(ictx,0);
	}
}


void slbt_reset_arguments(struct slbt_exec_ctx * ectx)
{
	struct slbt_exec_ctx_impl *	ictx;
	uintptr_t			addr;

	addr = (uintptr_t)ectx - offsetof(struct slbt_exec_ctx_impl,ctx);
	ictx = (struct slbt_exec_ctx_impl *)addr;
	memcpy(ictx->args,ictx->shadow,ictx->size);
}


void slbt_reset_placeholders(struct slbt_exec_ctx * ectx)
{
	*ectx->dpic = "-USLIBTOOL_PLACEHOLDER_DPIC";
	*ectx->fpic = "-USLIBTOOL_PLACEHOLDER_FPIC";
	*ectx->cass = "-USLIBTOOL_PLACEHOLDER_COMPILE_ASSEMBLE";

	*ectx->noundef = "-USLIBTOOL_PLACEHOLDER_NO_UNDEFINED";
	*ectx->soname  = "-USLIBTOOL_PLACEHOLDER_SONAME";
	*ectx->lsoname = "-USLIBTOOL_PLACEHOLDER_LSONAME";
	*ectx->symdefs = "-USLIBTOOL_PLACEHOLDER_SYMDEF_SWITCH";
	*ectx->symfile = "-USLIBTOOL_PLACEHOLDER_SYMDEF_FILE";

	*ectx->lout[0] = "-USLIBTOOL_PLACEHOLDER_OUTPUT_SWITCH";
	*ectx->lout[1] = "-USLIBTOOL_PLACEHOLDER_OUTPUT_FILE";

	*ectx->rpath[0] = "-USLIBTOOL_PLACEHOLDER_RPATH_SWITCH";
	*ectx->rpath[1] = "-USLIBTOOL_PLACEHOLDER_RPATH_DIR";

	ectx->mout[0]  = 0;
	ectx->mout[1]  = 0;

	*ectx->sentinel= 0;
}

void slbt_disable_placeholders(struct slbt_exec_ctx * ectx)
{
	*ectx->dpic = 0;
	*ectx->fpic = 0;
	*ectx->cass = 0;

	*ectx->noundef = 0;
	*ectx->soname  = 0;
	*ectx->lsoname = 0;
	*ectx->symdefs = 0;
	*ectx->symfile = 0;

	*ectx->lout[0] = 0;
	*ectx->lout[1] = 0;

	*ectx->rpath[0] = 0;
	*ectx->rpath[1] = 0;

	*ectx->sentinel= 0;
}
