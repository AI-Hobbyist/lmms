INCLUDE(InstallHelpers)

INSTALL_DATA_SUBDIRS("themes" "*.png;*.svg;*.css")
install(DIRECTORY default/trigger-lcd DESTINATION "${LMMS_DATA_DIR}/themes/default")
