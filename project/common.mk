API_SRCS = \
	src/driver/slbt_amain.c \
	src/driver/slbt_driver_ctx.c \
	src/helper/slbt_archive_import.c \
	src/helper/slbt_copy_file.c \
	src/helper/slbt_dump_machine.c \
	src/logic/slbt_exec_compile.c \
	src/logic/slbt_exec_ctx.c \
	src/logic/slbt_exec_execute.c \
	src/logic/slbt_exec_install.c \
	src/logic/slbt_exec_link.c \
	src/logic/slbt_exec_uninstall.c \
	src/output/slbt_output_config.c \
	src/output/slbt_output_error.c \
	src/output/slbt_output_exec.c \
	src/skin/slbt_skin_default.c \
	src/skin/slbt_skin_install.c \
	src/skin/slbt_skin_uninstall.c \

INTERNAL_SRCS = \
	src/internal/$(PACKAGE)_errinfo_impl.c \
	src/internal/$(PACKAGE)_libmeta_impl.c \
	src/internal/$(PACKAGE)_objmeta_impl.c \
	src/internal/$(PACKAGE)_symlink_impl.c \

APP_SRCS = \
	src/slibtool.c

COMMON_SRCS = $(API_SRCS) $(INTERNAL_SRCS)
