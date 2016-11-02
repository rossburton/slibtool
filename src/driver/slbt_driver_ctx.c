/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016  Z. Gilboa                                  */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#define ARGV_DRIVER

#include <slibtool/slibtool.h>
#include "slibtool_version.h"
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "argv/argv.h"

/* package info */
static const struct slbt_source_version slbt_src_version = {
	SLBT_TAG_VER_MAJOR,
	SLBT_TAG_VER_MINOR,
	SLBT_TAG_VER_PATCH,
	SLIBTOOL_GIT_VERSION
};

/* flavor settings */
#define SLBT_FLAVOR_SETTINGS(flavor,bfmt,arp,ars,dsop,dsos,exep,exes,impp,imps,ldenv) \
	static const struct slbt_flavor_settings flavor = {		   \
		bfmt,arp,ars,dsop,dsos,exep,exes,impp,imps,ldenv}

SLBT_FLAVOR_SETTINGS(host_flavor_default, "elf",  "lib",".a", "lib",".so",    "","",     "",   "",       "LD_LIBRARY_PATH");
SLBT_FLAVOR_SETTINGS(host_flavor_midipix, "pe",   "lib",".a", "lib",".so",    "","",     "lib",".lib.a", "PATH");
SLBT_FLAVOR_SETTINGS(host_flavor_mingw,   "pe",   "lib",".a", "lib",".dll",   "",".exe", "lib",".dll.a", "PATH");
SLBT_FLAVOR_SETTINGS(host_flavor_cygwin,  "pe",   "lib",".a", "lib",".dll",   "",".exe", "lib",".dll.a", "PATH");
SLBT_FLAVOR_SETTINGS(host_flavor_darwin,  "macho","lib",".a", "lib",".dylib", "","",     "",   "",       "DYLD_LIBRARY_PATH");


/* annotation strings */
static const char cfgexplicit[] = "command-line argument";
static const char cfghost[]     = "derived from <host>";
static const char cfgtarget[]   = "derived from <target>";
static const char cfgcompiler[] = "derived from <compiler>";
static const char cfgnmachine[] = "native (derived from -dumpmachine)";
static const char cfgxmachine[] = "foreign (derived from -dumpmachine)";
static const char cfgnative[]   = "native";

/* elf rpath */
static const char*ldrpath_elf[] = {
	"/lib",
	"/lib/64",
	"/usr/lib",
	"/usr/lib64",
	"/usr/local/lib",
	"/usr/local/lib64",
	0};

static const char aclr_reset [] = "\x1b[0m";
static const char aclr_bold  [] = "\x1b[1m";
static const char aclr_red   [] = "\x1b[31m";
static const char aclr_green [] = "\x1b[32m";
static const char aclr_yellow[] = "\x1b[33m";
static const char aclr_blue  [] = "\x1b[34m";
static const char aclr_cyan  [] = "\x1b[36m";
static const char aclr_white [] = "\x1b[37m";

struct slbt_split_vector {
	char **		targv;
	char **		cargv;
};

struct slbt_driver_ctx_alloc {
	struct argv_meta *		meta;
	struct slbt_driver_ctx_impl	ctx;
	uint64_t			guard;
};

static void slbt_output_raw_vector(char ** argv, char ** envp)
{
	char **		parg;
	char *		dot;
	const char *	color;
	bool		fcolor;

	(void)envp;

	if ((fcolor = isatty(STDOUT_FILENO)))
		fprintf(stderr,"%s%s",aclr_bold,aclr_red);

	fprintf(stderr,"\n\n\n%s",argv[0]);

	for (parg=&argv[1]; *parg; parg++) {
		if (!fcolor)
			color = "";
		else if (*parg[0] == '-')
			color = aclr_blue;
		else if (!(dot = strrchr(*parg,'.')))
			color = aclr_green;
		else if (!(strcmp(dot,".lo")))
			color = aclr_cyan;
		else if (!(strcmp(dot,".la")))
			color = aclr_yellow;
		else
			color = aclr_white;

		fprintf(stderr," %s%s",color,*parg);
	}

	fprintf(stderr,"%s\n\n",fcolor ? aclr_reset : "");
}

static uint32_t slbt_argv_flags(uint32_t flags)
{
	uint32_t ret = 0;

	if (flags & SLBT_DRIVER_VERBOSITY_NONE)
		ret |= ARGV_VERBOSITY_NONE;

	if (flags & SLBT_DRIVER_VERBOSITY_ERRORS)
		ret |= ARGV_VERBOSITY_ERRORS;

	if (flags & SLBT_DRIVER_VERBOSITY_STATUS)
		ret |= ARGV_VERBOSITY_STATUS;

	return ret;
}

static int slbt_driver_usage(
	const char *			program,
	const char *			arg,
	const struct argv_option *	options,
	struct argv_meta *		meta)
{
	char header[512];

	snprintf(header,sizeof(header),
		"Usage: %s [options] <file>...\n" "Options:\n",
		program);

	argv_usage(stdout,header,options,arg);
	argv_free(meta);

	return SLBT_USAGE;
}

static struct slbt_driver_ctx_impl * slbt_driver_ctx_alloc(
	struct argv_meta *		meta,
	const struct slbt_common_ctx *	cctx)
{
	struct slbt_driver_ctx_alloc *	ictx;
	size_t				size;
	int				elements;

	size =  sizeof(struct slbt_driver_ctx_alloc);

	if (!(ictx = calloc(1,size)))
		return 0;

	if (cctx)
		memcpy(&ictx->ctx.cctx,cctx,sizeof(*cctx));

	elements = sizeof(ictx->ctx.erribuf) / sizeof(*ictx->ctx.erribuf);

	ictx->ctx.errinfp  = &ictx->ctx.erriptr[0];
	ictx->ctx.erricap  = &ictx->ctx.erriptr[--elements];

	ictx->meta = meta;
	ictx->ctx.ctx.errv  = ictx->ctx.errinfp;
	return &ictx->ctx;
}

static int slbt_get_driver_ctx_fail(struct argv_meta * meta)
{
	argv_free(meta);
	return -1;
}

static int slbt_split_argv(
	char **				argv,
	uint32_t			flags,
	struct slbt_split_vector *	sargv)
{
	int				i;
	int				argc;
	const char *			program;
	char *				compiler;
	char **				targv;
	char **				cargv;
	struct argv_meta *		meta;
	struct argv_entry *		entry;
	struct argv_entry *		mode;
	struct argv_entry *		config;
	struct argv_entry *		finish;
	const struct argv_option *	option;
	const struct argv_option *	options = slbt_default_options;
	struct argv_ctx			ctx = {ARGV_VERBOSITY_NONE,
						ARGV_MODE_SCAN,
						0,0,0,0,0,0,0};

	program = argv_program_name(argv[0]);

	/* missing arguments? */
	if (!argv[1] && (flags & SLBT_DRIVER_VERBOSITY_USAGE))
		return slbt_driver_usage(program,0,options,0);

	/* initial argv scan: ... --mode=xxx ... <compiler> ... */
	argv_scan(argv,options,&ctx,0);

	/* invalid slibtool arguments? */
	if (ctx.erridx && !ctx.unitidx) {
		if (flags & SLBT_DRIVER_VERBOSITY_ERRORS)
			argv_get(
				argv,options,
				slbt_argv_flags(flags));
		return -1;
	}

	/* obtain slibtool's own arguments */
	compiler = argv[ctx.unitidx];
	argv[ctx.unitidx] = 0;

	meta = argv_get(argv,options,ARGV_VERBOSITY_NONE);
	argv[ctx.unitidx] = compiler;

	/* missing all of --mode, --config, and --finish? */
	for (mode=0, config=0, finish=0, entry=meta->entries; entry->fopt; entry++)
		if (entry->tag == TAG_MODE)
			mode = entry;
		else if (entry->tag == TAG_CONFIG)
			config = entry;
		else if (entry->tag == TAG_FINISH)
			finish = entry;

	argv_free(meta);

	if (!mode && !config && !finish) {
		fprintf(stderr,
			"%s: error: --mode must be specified.\n",
			program);
		return -1;
	}

	/* missing compiler? */
	if (!ctx.unitidx && !finish) {
		if (flags & SLBT_DRIVER_VERBOSITY_ERRORS)
			fprintf(stderr,
				"%s: error: <compiler> is missing.\n",
				program);
		return -1;
	}

	/* allocate split vectors */
	for (argc=0, targv=argv; *targv; targv++)
		argc++;

	if ((sargv->targv = calloc(2*(argc+1),sizeof(char *))))
		sargv->cargv = sargv->targv + argc + 1;
	else
		return -1;

	/* split vectors: slibtool's own options */
	for (i=0; i<ctx.unitidx; i++)
		sargv->targv[i] = argv[i];

	/* split vectors: legacy mixture */
	options = option_from_tag(
			slbt_default_options,
			TAG_OUTPUT);

	targv = sargv->targv + i;
	cargv = sargv->cargv;

	for (; i<argc; i++) {
		if (argv[i][0] != '-') {
			if (argv[i+1] && (argv[i+1][0] == '+')
					&& (argv[i+1][1] == '=')
					&& (argv[i+1][2] == 0)
					&& !(strrchr(argv[i],'.')))
				/* libfoo_la_LDFLAGS += -Wl,.... */
				i++;
			else
				*cargv++ = argv[i];

		} else if (argv[i][1] == 'o') {
			*targv++ = argv[i];

			if (argv[i][2] == 0)
				*targv++ = argv[++i];
		}

		else if ((argv[i][1] == 'W')  && (argv[i][2] == 'c'))
			*cargv++ = argv[i];

		else if (!(strcmp("Xcompiler",&argv[i][1])))
			*cargv++ = argv[++i];

		else if (!(strncmp("-target=",&argv[i][1],strlen("-target="))))
			*targv++ = argv[i];

		else if (!(strcmp("-target",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if ((argv[i][1] == 'R')  && (argv[i][2] == 0)) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (argv[i][1] == 'R') {
			*targv++ = argv[i];

		} else if (!(strcmp("bindir",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("shrext",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("rpath",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("release",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("dlopen",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("export-symbols",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("export-symbols-regex",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("version-info",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("version-number",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else {
			for (option=options; option->long_name; option++)
				if (!(strcmp(option->long_name,&argv[i][1])))
					break;

			if (option->long_name)
				*targv++ = argv[i];
			else
				*cargv++ = argv[i];
		}
	}

	return 0;
}

static int slbt_init_host_params(
	const struct slbt_common_ctx *	cctx,
	struct slbt_host_strs *		drvhost,
	struct slbt_host_params *	host,
	struct slbt_host_params *	cfgmeta)
{
	size_t		toollen;
	char *		dash;
	char *		base;
	char *		mark;
	const char *	machine;
	bool		ftarget       = false;
	bool		fhost         = false;
	bool		fcompiler     = false;
	bool		fnative       = false;
	bool		fdumpmachine  = false;
	char		buf[256];

	/* base */
	if ((base = strrchr(cctx->cargv[0],'/')))
		base++;
	else
		base = cctx->cargv[0];

	if ((cctx->mode == SLBT_MODE_COMPILE) || (cctx->mode == SLBT_MODE_LINK))
		fdumpmachine = true;

	/* support the portbld <--> unknown synonym */
	if (!(drvhost->machine = strdup(SLBT_MACHINE)))
		return -1;

	if ((mark = strstr(drvhost->machine,"-portbld-")))
		memcpy(mark,"-unknown",8);

	/* host */
	if (host->host) {
		cfgmeta->host = cfgexplicit;
		fhost         = true;
	} else if (cctx->target) {
		host->host    = cctx->target;
		cfgmeta->host = cfgtarget;
		ftarget       = true;
	} else if (strrchr(base,'-')) {
		if (!(drvhost->host = strdup(cctx->cargv[0])))
			return -1;

		dash          = strrchr(drvhost->host,'-');
		*dash         = 0;
		host->host    = drvhost->host;
		cfgmeta->host = cfgcompiler;
		fcompiler     = true;
	} else if (fdumpmachine && !(slbt_dump_machine(
				cctx->cargv[0],
				buf,sizeof(buf)))) {
		if (!(drvhost->host = strdup(buf)))
			return -1;

		host->host    = drvhost->host;
		fcompiler     = true;
		fnative       = (!(strcmp(host->host,drvhost->machine)));
		cfgmeta->host = fnative ? cfgnmachine : cfgxmachine;
	} else {
		host->host    = drvhost->machine;
		cfgmeta->host = cfgnmachine;
		fnative       = true;
	}

	/* flavor */
	if (host->flavor) {
		cfgmeta->flavor = cfgexplicit;
	} else {
		if (fhost) {
			machine         = host->host;
			cfgmeta->flavor = cfghost;
		} else if (ftarget) {
			machine         = cctx->target;
			cfgmeta->flavor = cfgtarget;
		} else if (fcompiler) {
			machine         = drvhost->host;
			cfgmeta->flavor = cfgcompiler;
		} else {
			machine         = drvhost->machine;
			cfgmeta->flavor = cfgnmachine;
		}

		dash = strrchr(machine,'-');
		cfgmeta->flavor = cfghost;

		if ((dash && !strcmp(dash,"-bsd")) || strstr(machine,"-bsd-"))
			host->flavor = "bsd";
		else if ((dash && !strcmp(dash,"-cygwin")) || strstr(machine,"-cygwin-"))
			host->flavor = "cygwin";
		else if ((dash && !strcmp(dash,"-darwin")) || strstr(machine,"-darwin"))
			host->flavor = "darwin";
		else if ((dash && !strcmp(dash,"-linux")) || strstr(machine,"-linux-"))
			host->flavor = "linux";
		else if ((dash && !strcmp(dash,"-midipix")) || strstr(machine,"-midipix-"))
			host->flavor = "midipix";
		else if ((dash && !strcmp(dash,"-mingw")) || strstr(machine,"-mingw-"))
			host->flavor = "mingw";
		else if ((dash && !strcmp(dash,"-mingw32")) || strstr(machine,"-mingw32-"))
			host->flavor = "mingw";
		else if ((dash && !strcmp(dash,"-mingw64")) || strstr(machine,"-mingw64-"))
			host->flavor = "mingw";
		else {
			host->flavor   = "default";
			cfgmeta->flavor = "fallback, unverified";
		}
	}

	/* toollen */
	toollen =  fnative ? 0 : strlen(host->host);
	toollen += strlen("-utility-name");

	/* ar */
	if (host->ar)
		cfgmeta->ar = cfgexplicit;
	else {
		if (!(drvhost->ar = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->ar,"ar");
			cfgmeta->ar = cfgnative;
		} else {
			sprintf(drvhost->ar,"%s-ar",host->host);
			cfgmeta->ar = cfghost;
		}

		host->ar = drvhost->ar;
	}

	/* ranlib */
	if (host->ranlib)
		cfgmeta->ranlib = cfgexplicit;
	else {
		if (!(drvhost->ranlib = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->ranlib,"ranlib");
			cfgmeta->ranlib = cfgnative;
		} else {
			sprintf(drvhost->ranlib,"%s-ranlib",host->host);
			cfgmeta->ranlib = cfghost;
		}

		host->ranlib = drvhost->ranlib;
	}

	/* dlltool */
	if (host->dlltool)
		cfgmeta->dlltool = cfgexplicit;

	else if (strcmp(host->flavor,"cygwin")
			&& strcmp(host->flavor,"midipix")
			&& strcmp(host->flavor,"mingw")) {
		host->dlltool = "";
		cfgmeta->dlltool = "not applicable";

	} else {
		if (!(drvhost->dlltool = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->dlltool,"dlltool");
			cfgmeta->dlltool = cfgnative;
		} else {
			sprintf(drvhost->dlltool,"%s-dlltool",host->host);
			cfgmeta->dlltool = cfghost;
		}

		host->dlltool = drvhost->dlltool;
	}

	return 0;
}

static void slbt_free_host_params(struct slbt_host_strs * host)
{
	if (host->machine)
		free(host->machine);

	if (host->host)
		free(host->host);

	if (host->flavor)
		free(host->flavor);

	if (host->ar)
		free(host->ar);

	if (host->ranlib)
		free(host->ranlib);

	if (host->dlltool)
		free(host->dlltool);

	memset(host,0,sizeof(*host));
}

static void slbt_init_flavor_settings(
	struct slbt_common_ctx *	cctx,
	const struct slbt_host_params * ahost,
	struct slbt_flavor_settings *	psettings)
{
	const struct slbt_host_params *     host;
	const struct slbt_flavor_settings * settings;

	host = ahost ? ahost : &cctx->host;

	if (!strcmp(host->flavor,"midipix"))
		settings = &host_flavor_midipix;
	else if (!strcmp(host->flavor,"mingw"))
		settings = &host_flavor_mingw;
	else if (!strcmp(host->flavor,"cygwin"))
		settings = &host_flavor_cygwin;
	else if (!strcmp(host->flavor,"darwin"))
		settings = &host_flavor_darwin;
	else
		settings = &host_flavor_default;

	if (!ahost) {
		if (!strcmp(settings->imagefmt,"elf"))
			cctx->drvflags |= SLBT_DRIVER_IMAGE_ELF;
		else if (!strcmp(settings->imagefmt,"pe"))
			cctx->drvflags |= SLBT_DRIVER_IMAGE_PE;
		else if (!strcmp(settings->imagefmt,"macho"))
			cctx->drvflags |= SLBT_DRIVER_IMAGE_MACHO;
	}

	memcpy(psettings,settings,sizeof(*settings));

	if (cctx->shrext)
		psettings->dsosuffix = cctx->shrext;
}

static int slbt_init_ldrpath(
	struct slbt_common_ctx *  cctx,
	struct slbt_host_params * host)
{
	char *         buf;
	const char **  ldrpath;

	if (!cctx->rpath || !(cctx->drvflags & SLBT_DRIVER_IMAGE_ELF)) {
		host->ldrpath = 0;
		return 0;
	}

	/* common? */
	for (ldrpath=ldrpath_elf; *ldrpath; ldrpath ++)
		if (!(strcmp(cctx->rpath,*ldrpath))) {
			host->ldrpath = 0;
			return 0;
		}

	/* buf */
	if (!(buf = malloc(12 + strlen(cctx->host.host))))
		return -1;

	/* /usr/{host}/lib */
	sprintf(buf,"/usr/%s/lib",cctx->host.host);

	if (!(strcmp(cctx->rpath,buf))) {
		host->ldrpath = 0;
		free(buf);
		return 0;
	}

	/* /usr/{host}/lib64 */
	sprintf(buf,"/usr/%s/lib64",cctx->host.host);

	if (!(strcmp(cctx->rpath,buf))) {
		host->ldrpath = 0;
		free(buf);
		return 0;
	}

	host->ldrpath = cctx->rpath;

	free(buf);
	return 0;
}

static int slbt_init_version_info(
	struct slbt_driver_ctx_impl *	ictx,
	struct slbt_version_info *	verinfo)
{
	int	current;
	int	revision;
	int	age;

	if (!verinfo->verinfo && !verinfo->vernumber)
		return 0;

	if (verinfo->vernumber) {
		sscanf(verinfo->vernumber,"%d:%d:%d",
			&verinfo->major,
			&verinfo->minor,
			&verinfo->revision);
		return 0;
	}

	current = revision = age = 0;

	sscanf(verinfo->verinfo,"%d:%d:%d",
		&current,&revision,&age);

	if (current < age) {
		if (ictx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
			fprintf(stderr,
				"%s: error: invalid version info: "
				"<current> may not be smaller than <age>.\n",
				argv_program_name(ictx->cctx.targv[0]));
		return -1;
	}

	verinfo->major    = current - age;
	verinfo->minor    = age;
	verinfo->revision = revision;

	return 0;
}

static int slbt_init_link_params(struct slbt_driver_ctx_impl * ctx)
{
	const char * program;
	const char * libname;
	const char * prefix;
	const char * base;
	char *       dot;
	bool         fmodule;

	program = argv_program_name(ctx->cctx.targv[0]);
	libname = 0;
	prefix  = 0;
	fmodule = false;

	/* output */
	if (!(ctx->cctx.output)) {
		if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
			fprintf(stderr,
				"%s: error: output file must be "
				"specified in link mode.\n",
				program);
		return -1;
	}

	/* executable? */
	if (!(dot = strrchr(ctx->cctx.output,'.')))
		return 0;

	/* todo: archive? library? wrapper? inlined function, avoid repetition */
	if ((base = strrchr(ctx->cctx.output,'/')))
		base++;
	else
		base = ctx->cctx.output;

	/* archive? */
	if (!strcmp(dot,ctx->cctx.settings.arsuffix)) {
		prefix = ctx->cctx.settings.arprefix;

		if (!strncmp(prefix,base,strlen(prefix)))
			libname = base;
		else {
			if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
				fprintf(stderr,
					"%s: error: output file prefix does "
					"not match its (archive) suffix; "
					"the expected prefix was '%s'\n",
					program,prefix);
			return -1;
		}
	}

	/* library? */
	else if (!strcmp(dot,ctx->cctx.settings.dsosuffix)) {
		prefix = ctx->cctx.settings.dsoprefix;

		if (!strncmp(prefix,base,strlen(prefix)))
			libname = base;
		else {
			if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
				fprintf(stderr,
					"%s: error: output file prefix does "
					"not match its (shared library) suffix; "
					"the expected prefix was '%s'\n",
					program,prefix);
			return -1;
		}
	}

	/* wrapper? */
	else if (!strcmp(dot,".la")) {
		prefix = ctx->cctx.settings.dsoprefix;

		if (!strncmp(prefix,base,strlen(prefix))) {
			libname = base;
			fmodule = !!(ctx->cctx.drvflags & SLBT_DRIVER_MODULE);
		} else if (ctx->cctx.drvflags & SLBT_DRIVER_MODULE) {
			libname = base;
			fmodule = true;
		} else {
			if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
				fprintf(stderr,
					"%s: error: output file prefix does "
					"not match its (libtool wrapper) suffix; "
					"the expected prefix was '%s'\n",
					program,prefix);
			return -1;
		}
	} else
		return 0;

	/* libname alloc */
	if (!fmodule)
		libname += strlen(prefix);

	if (!(ctx->libname = strdup(libname)))
		return -1;

	dot  = strrchr(ctx->libname,'.');
	*dot = 0;

	ctx->cctx.libname = ctx->libname;

	return 0;
}

int slbt_get_driver_ctx(
	char **				argv,
	char **				envp,
	uint32_t			flags,
	struct slbt_driver_ctx **	pctx)
{
	struct slbt_split_vector	sargv;
	struct slbt_driver_ctx_impl *	ctx;
	struct slbt_common_ctx		cctx;
	const struct argv_option *	options;
	struct argv_meta *		meta;
	struct argv_entry *		entry;
	const char *			program;

	options = slbt_default_options;

	if (slbt_split_argv(argv,flags,&sargv))
		return -1;

	if (!(meta = argv_get(sargv.targv,options,slbt_argv_flags(flags))))
		return -1;

	program = argv_program_name(argv[0]);
	memset(&cctx,0,sizeof(cctx));

	/* shared and static objects: enable by default, disable by ~switch */
	cctx.drvflags = flags | SLBT_DRIVER_SHARED | SLBT_DRIVER_STATIC;

	/* full annotation when annotation is on; */
	cctx.drvflags |= SLBT_DRIVER_ANNOTATE_FULL;

	/* get options */
	for (entry=meta->entries; entry->fopt || entry->arg; entry++) {
		if (entry->fopt) {
			switch (entry->tag) {
				case TAG_HELP:
				case TAG_HELP_ALL:
					if (flags & SLBT_DRIVER_VERBOSITY_USAGE)
						return slbt_driver_usage(program,entry->arg,options,meta);

				case TAG_VERSION:
					cctx.drvflags |= SLBT_DRIVER_VERSION;
					break;

				case TAG_MODE:
					if (!strcmp("clean",entry->arg))
						cctx.mode = SLBT_MODE_CLEAN;

					else if (!strcmp("compile",entry->arg))
						cctx.mode = SLBT_MODE_COMPILE;

					else if (!strcmp("execute",entry->arg))
						cctx.mode = SLBT_MODE_EXECUTE;

					else if (!strcmp("finish",entry->arg))
						cctx.mode = SLBT_MODE_FINISH;

					else if (!strcmp("install",entry->arg))
						cctx.mode = SLBT_MODE_INSTALL;

					else if (!strcmp("link",entry->arg))
						cctx.mode = SLBT_MODE_LINK;

					else if (!strcmp("uninstall",entry->arg))
						cctx.mode = SLBT_MODE_UNINSTALL;
					break;

				case TAG_FINISH:
					cctx.mode = SLBT_MODE_FINISH;
					break;

				case TAG_DRY_RUN:
					cctx.drvflags |= SLBT_DRIVER_DRY_RUN;
					break;

				case TAG_TAG:
					if (!strcmp("CC",entry->arg))
						cctx.tag = SLBT_TAG_CC;

					else if (!strcmp("CXX",entry->arg))
						cctx.tag = SLBT_TAG_CXX;

					else if (!strcmp("NASM",entry->arg))
						cctx.tag = SLBT_TAG_NASM;

					else if (!strcmp("disable-static",entry->arg))
						cctx.drvflags |= SLBT_DRIVER_DISABLE_STATIC;

					else if (!strcmp("disable-shared",entry->arg))
						cctx.drvflags |= SLBT_DRIVER_DISABLE_SHARED;
					break;

				case TAG_CONFIG:
					cctx.drvflags |= SLBT_DRIVER_CONFIG;
					break;

				case TAG_DEBUG:
					cctx.drvflags |= SLBT_DRIVER_DEBUG;
					break;

				case TAG_FEATURES:
					cctx.drvflags |= SLBT_DRIVER_FEATURES;
					break;

				case TAG_LEGABITS:
					if (!entry->arg)
						cctx.drvflags |= SLBT_DRIVER_LEGABITS;

					else if (!strcmp("enabled",entry->arg))
						cctx.drvflags |= SLBT_DRIVER_LEGABITS;

					else
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_LEGABITS;

					break;

				case TAG_WARNINGS:
					if (!strcmp("all",entry->arg))
						cctx.warnings = SLBT_WARNING_LEVEL_ALL;

					else if (!strcmp("error",entry->arg))
						cctx.warnings = SLBT_WARNING_LEVEL_ERROR;

					else if (!strcmp("none",entry->arg))
						cctx.warnings = SLBT_WARNING_LEVEL_NONE;
					break;

				case TAG_ANNOTATE:
					if (!strcmp("always",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_ANNOTATE_ALWAYS;
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_ANNOTATE_NEVER;

					} else if (!strcmp("never",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_ANNOTATE_NEVER;
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_ANNOTATE_ALWAYS;

					} else if (!strcmp("minimal",entry->arg)) {
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_ANNOTATE_FULL;

					} else if (!strcmp("full",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_ANNOTATE_FULL;
					}

					break;

				case TAG_DEPS:
					cctx.drvflags |= SLBT_DRIVER_DEPS;
					break;

				case TAG_SILENT:
					cctx.drvflags |= SLBT_DRIVER_SILENT;
					break;

				case TAG_VERBOSE:
					cctx.drvflags |= SLBT_DRIVER_VERBOSE;
					break;

				case TAG_HOST:
					cctx.host.host = entry->arg;
					break;

				case TAG_FLAVOR:
					cctx.host.flavor = entry->arg;
					break;

				case TAG_AR:
					cctx.host.ar = entry->arg;
					break;

				case TAG_RANLIB:
					cctx.host.ranlib = entry->arg;
					break;

				case TAG_DLLTOOL:
					cctx.host.dlltool = entry->arg;
					break;

				case TAG_OUTPUT:
					cctx.output = entry->arg;
					break;

				case TAG_SHREXT:
					cctx.shrext = entry->arg;
					break;

				case TAG_RPATH:
					cctx.rpath = entry->arg;
					break;

				case TAG_RELEASE:
					cctx.release = entry->arg;
					cctx.drvflags |= SLBT_DRIVER_AVOID_VERSION;
					break;

				case TAG_DLOPEN:
					break;

				case TAG_EXPSYM_FILE:
					cctx.symfile = entry->arg;
					break;

				case TAG_EXPSYM_REGEX:
					cctx.regex = entry->arg;
					break;

				case TAG_VERSION_INFO:
					cctx.verinfo.verinfo = entry->arg;
					break;

				case TAG_VERSION_NUMBER:
					cctx.verinfo.vernumber = entry->arg;
					break;

				case TAG_TARGET:
					cctx.target = entry->arg;
					break;

				case TAG_PREFER_PIC:
					cctx.drvflags |= SLBT_DRIVER_PRO_PIC;
					break;

				case TAG_PREFER_NON_PIC:
					cctx.drvflags |= SLBT_DRIVER_ANTI_PIC;
					break;

				case TAG_NO_UNDEFINED:
					cctx.drvflags |= SLBT_DRIVER_NO_UNDEFINED;
					break;

				case TAG_MODULE:
					cctx.drvflags |= SLBT_DRIVER_MODULE;
					break;

				case TAG_ALL_STATIC:
					cctx.drvflags |= SLBT_DRIVER_ALL_STATIC;
					break;

				case TAG_DISABLE_STATIC:
					cctx.drvflags |= SLBT_DRIVER_DISABLE_STATIC;
					break;

				case TAG_DISABLE_SHARED:
					cctx.drvflags |= SLBT_DRIVER_DISABLE_SHARED;
					break;

				case TAG_AVOID_VERSION:
					cctx.drvflags |= SLBT_DRIVER_AVOID_VERSION;
					break;

				case TAG_SHARED:
					cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_STATIC;
					break;

				case TAG_STATIC:
					cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_SHARED;
					break;
			}
		}
	}

	/* debug: raw argument vector */
	if (cctx.drvflags & SLBT_DRIVER_DEBUG)
		slbt_output_raw_vector(argv,envp);

	/* -o in install mode means USER */
	if ((cctx.mode == SLBT_MODE_INSTALL) && cctx.output) {
		cctx.user   = cctx.output;
		cctx.output = 0;
	}

	/* driver context */
	if (!(ctx = slbt_driver_ctx_alloc(meta,&cctx)))
		return slbt_get_driver_ctx_fail(meta);

	ctx->ctx.program	= program;
	ctx->ctx.cctx		= &ctx->cctx;
	ctx->targv		= sargv.targv;
	ctx->cargv		= sargv.cargv;

	ctx->cctx.targv		= sargv.targv;
	ctx->cctx.cargv		= sargv.cargv;

	/* host params */
	if ((cctx.drvflags & SLBT_DRIVER_HEURISTICS)
			|| (cctx.drvflags & SLBT_DRIVER_CONFIG)
			|| (cctx.mode != SLBT_MODE_COMPILE)) {
		if (slbt_init_host_params(
				&ctx->cctx,
				&ctx->host,
				&ctx->cctx.host,
				&ctx->cctx.cfgmeta)) {
			slbt_free_driver_ctx(&ctx->ctx);
			return -1;
		} else
			slbt_init_flavor_settings(
				&ctx->cctx,0,
				&ctx->cctx.settings);
	}

	/* ldpath */
	if (slbt_init_ldrpath(&ctx->cctx,&ctx->cctx.host)) {
		slbt_free_driver_ctx(&ctx->ctx);
		return -1;
	}

	/* version info */
	if (slbt_init_version_info(ctx,&ctx->cctx.verinfo)) {
		slbt_free_driver_ctx(&ctx->ctx);
		return -1;
	}

	/* link params */
	if (cctx.mode == SLBT_MODE_LINK)
		if (slbt_init_link_params(ctx)) {
			slbt_free_driver_ctx(&ctx->ctx);
			return -1;
		}

	*pctx = &ctx->ctx;
	return SLBT_OK;
}

int slbt_create_driver_ctx(
	const struct slbt_common_ctx *	cctx,
	struct slbt_driver_ctx **	pctx)
{
	struct argv_meta *		meta;
	struct slbt_driver_ctx_impl *	ctx;
	char *				argv[] = {"slibtool_driver",0};

	if (!(meta = argv_get(argv,slbt_default_options,0)))
		return -1;

	if (!(ctx = slbt_driver_ctx_alloc(meta,cctx)))
		return slbt_get_driver_ctx_fail(0);

	ctx->ctx.cctx = &ctx->cctx;
	memcpy(&ctx->cctx,cctx,sizeof(*cctx));
	*pctx = &ctx->ctx;
	return SLBT_OK;
}

static void slbt_free_driver_ctx_impl(struct slbt_driver_ctx_alloc * ictx)
{
	if (ictx->ctx.targv)
		free(ictx->ctx.targv);

	if (ictx->ctx.libname)
		free(ictx->ctx.libname);

	slbt_free_host_params(&ictx->ctx.host);
	slbt_free_host_params(&ictx->ctx.ahost);
	argv_free(ictx->meta);
	free(ictx);
}

void slbt_free_driver_ctx(struct slbt_driver_ctx * ctx)
{
	struct slbt_driver_ctx_alloc *	ictx;
	uintptr_t			addr;

	if (ctx) {
		addr = (uintptr_t)ctx - offsetof(struct slbt_driver_ctx_impl,ctx);
		addr = addr - offsetof(struct slbt_driver_ctx_alloc,ctx);
		ictx = (struct slbt_driver_ctx_alloc *)addr;
		slbt_free_driver_ctx_impl(ictx);
	}
}

void slbt_reset_alternate_host(const struct slbt_driver_ctx * ctx)
{
	struct slbt_driver_ctx_alloc *	ictx;
	uintptr_t			addr;

	addr = (uintptr_t)ctx - offsetof(struct slbt_driver_ctx_alloc,ctx);
	addr = addr - offsetof(struct slbt_driver_ctx_impl,ctx);
	ictx = (struct slbt_driver_ctx_alloc *)addr;

	slbt_free_host_params(&ictx->ctx.ahost);
}

int  slbt_set_alternate_host(
	const struct slbt_driver_ctx *	ctx,
	const char *			host,
	const char *			flavor)
{
	struct slbt_driver_ctx_alloc *	ictx;
	uintptr_t			addr;

	addr = (uintptr_t)ctx - offsetof(struct slbt_driver_ctx_alloc,ctx);
	addr = addr - offsetof(struct slbt_driver_ctx_impl,ctx);
	ictx = (struct slbt_driver_ctx_alloc *)addr;
	slbt_free_host_params(&ictx->ctx.ahost);

	if (!(ictx->ctx.ahost.host = strdup(host)))
		return SLBT_SYSTEM_ERROR(ctx);

	if (!(ictx->ctx.ahost.flavor = strdup(flavor))) {
		slbt_free_host_params(&ictx->ctx.ahost);
		return SLBT_SYSTEM_ERROR(ctx);
	}

	ictx->ctx.cctx.ahost.host   = ictx->ctx.ahost.host;
	ictx->ctx.cctx.ahost.flavor = ictx->ctx.ahost.flavor;

	if (slbt_init_host_params(
			ctx->cctx,
			&ictx->ctx.ahost,
			&ictx->ctx.cctx.ahost,
			&ictx->ctx.cctx.acfgmeta)) {
		slbt_free_host_params(&ictx->ctx.ahost);
		return SLBT_CUSTOM_ERROR(ctx,SLBT_ERR_HOST_INIT);
	}

	slbt_init_flavor_settings(
		&ictx->ctx.cctx,
		&ictx->ctx.cctx.ahost,
		&ictx->ctx.cctx.asettings);

	if (slbt_init_ldrpath(
			&ictx->ctx.cctx,
			&ictx->ctx.cctx.ahost)) {
		slbt_free_host_params(&ictx->ctx.ahost);
		return SLBT_CUSTOM_ERROR(ctx,SLBT_ERR_LDRPATH_INIT);
	}

	return 0;
}

const struct slbt_source_version * slbt_source_version(void)
{
	return &slbt_src_version;
}
