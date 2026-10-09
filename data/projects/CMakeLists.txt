INCLUDE(InstallHelpers)

INSTALL_DATA_SUBDIRS("projects" "*.mmp")
INSTALL_DATA_SUBDIRS("projects" "*.mmpz")
INSTALL_DATA_SUBDIRS("projects" "*.mpt")

INSTALL(FILES "${PROJECT_SOURCE_DIR}/plugins/SVSExample/SVSExample-demo.mmp"
		DESTINATION "${LMMS_DATA_DIR}/projects/svs")
