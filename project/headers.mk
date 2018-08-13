API_HEADERS = \
	$(PROJECT_DIR)/include/$(PACKAGE)/slibtool.h \
	$(PROJECT_DIR)/include/$(PACKAGE)/slibtool_api.h \

INTERNAL_HEADERS = \
	$(PROJECT_DIR)/src/internal/argv/argv.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_dprintf_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_driver_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_errinfo_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_install_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_lconf_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_mapfile_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_metafile_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_mkdir_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_readlink_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_spawn_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_symlink_impl.h \
	$(PROJECT_DIR)/src/internal/$(PACKAGE)_uninstall_impl.h \

ALL_HEADERS = $(API_HEADERS) $(INTERNAL_HEADERS)
