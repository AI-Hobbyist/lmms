# Build native plug-in code in independent projects so neither Qt nor the VST3
# SDK is linked into the DAW, and x86 does not require a separate Qt installation.
if(NOT MSVC)
	message(FATAL_ERROR "Windows VST3 helpers currently require the MSVC toolchain")
endif()
include(ExternalProject)
set(LMMS_VST3_SDK_ROOT "${LMMS_SOURCE_DIR}/vst3sdk" CACHE PATH "Local Steinberg VST3 SDK checkout")
if(NOT EXISTS "${LMMS_VST3_SDK_ROOT}/pluginterfaces/vst/ivstaudioprocessor.h")
	message(FATAL_ERROR "WANT_VST3 requires a complete local LMMS_VST3_SDK_ROOT")
endif()
foreach(vst_bits 64 32)
	if(vst_bits EQUAL 64)
		set(vst_platform x64)
		set(vst_arch x64)
		set(vst_destination "${LMMS_BINARY_DIR}/plugins")
		set(vst_install_destination "${PLUGIN_DIR}")
	else()
		set(vst_platform Win32)
		set(vst_arch x86)
		set(vst_destination "${LMMS_BINARY_DIR}/plugins/32")
		set(vst_install_destination "${PLUGIN_DIR}/32")
	endif()
	set(vst_binary_dir "${LMMS_BINARY_DIR}/vst-host/${vst_arch}")
	ExternalProject_Add(RemoteVstHost${vst_bits}
		SOURCE_DIR "${LMMS_SOURCE_DIR}/tools/vsthost"
		BINARY_DIR "${vst_binary_dir}"
		CMAKE_GENERATOR "${CMAKE_GENERATOR}"
		CMAKE_GENERATOR_PLATFORM "${vst_platform}"
		CMAKE_ARGS "-DLMMS_VST3_SDK_ROOT=${LMMS_VST3_SDK_ROOT}" "-DBUILD_TESTING=${BUILD_TESTING}"
		BUILD_ALWAYS ON
		INSTALL_COMMAND "${CMAKE_COMMAND}" -E make_directory "${vst_destination}"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different
			"${vst_binary_dir}/$<CONFIG>/RemoteVstHost.exe" "${vst_destination}/RemoteVstHost${vst_bits}.exe")
	install(PROGRAMS "${vst_destination}/RemoteVstHost${vst_bits}.exe" DESTINATION "${vst_install_destination}")
endforeach()

if(BUILD_TESTING)
	set(vst_test64 "${LMMS_BINARY_DIR}/vst-host/x64/$<CONFIG>")
	set(vst_test32 "${LMMS_BINARY_DIR}/vst-host/x86/$<CONFIG>")
	add_test(NAME Vst3Catalog64 COMMAND "${vst_test64}/Vst3CatalogTest.exe"
		"${LMMS_BINARY_DIR}/plugins/RemoteVstHost64.exe" "${vst_test64}")
	add_test(NAME Vst3CatalogCrossABI COMMAND "${vst_test64}/Vst3CatalogTest.exe"
		"${LMMS_BINARY_DIR}/plugins/32/RemoteVstHost32.exe" "${vst_test32}" x86)
	add_test(NAME Vst3Processing64 COMMAND "${vst_test64}/Vst3ProcessingTest.exe"
		"${LMMS_BINARY_DIR}/plugins/RemoteVstHost64.exe" "${vst_test64}/Vst3Native.vst3")
	add_test(NAME Vst3ProcessingCrossABI COMMAND "${vst_test64}/Vst3ProcessingTest.exe"
		"${LMMS_BINARY_DIR}/plugins/32/RemoteVstHost32.exe" "${vst_test32}/Vst3Native.vst3")
	add_test(NAME Vst3Lifecycle64 COMMAND "${vst_test64}/Vst3LifecycleTest.exe"
		"${LMMS_BINARY_DIR}/plugins/RemoteVstHost64.exe" "${vst_test64}/Vst3Native.vst3")
	add_test(NAME Vst3LifecycleCrossABI COMMAND "${vst_test64}/Vst3LifecycleTest.exe"
		"${LMMS_BINARY_DIR}/plugins/32/RemoteVstHost32.exe" "${vst_test32}/Vst3Native.vst3")
	set_tests_properties(Vst3Lifecycle64 Vst3LifecycleCrossABI PROPERTIES TIMEOUT 60 LABELS "vsthost;vst3;native;editor;lifecycle")
	set_tests_properties(Vst3Catalog64 Vst3CatalogCrossABI Vst3Processing64 Vst3ProcessingCrossABI
		PROPERTIES TIMEOUT 30 LABELS "vsthost;vst3;native")
endif()
