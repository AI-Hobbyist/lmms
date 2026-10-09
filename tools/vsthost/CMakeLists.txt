cmake_minimum_required(VERSION 3.25)
project(LmmsVstHost LANGUAGES CXX)

if(NOT WIN32)
	message(FATAL_ERROR "The native VST host is currently built for Windows only")
endif()

get_filename_component(LMMS_VST3_DEFAULT_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../vst3sdk" ABSOLUTE)
set(LMMS_VST3_SDK_ROOT
	"${LMMS_VST3_DEFAULT_SDK}"
	CACHE PATH "Local Steinberg VST3 SDK checkout")
if(NOT EXISTS "${LMMS_VST3_SDK_ROOT}/pluginterfaces/vst/ivstaudioprocessor.h"
   OR NOT EXISTS "${LMMS_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp")
	message(FATAL_ERROR "LMMS_VST3_SDK_ROOT must contain the complete VST3 SDK and its submodules")
endif()

# Keep this project independent of Qt so the same native adapter can be compiled
# for Win32 and x64, while the DAW continues to use its existing x64 Qt toolchain.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES
	OFF
	CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES
	OFF
	CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT
	OFF
	CACHE BOOL "" FORCE)
set(SMTG_ENABLE_AUV2_BUILDS
	OFF
	CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK
	OFF
	CACHE BOOL "" FORCE)
add_subdirectory("${LMMS_VST3_SDK_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/sdk" EXCLUDE_FROM_ALL)

add_library(LmmsVst3Loader STATIC "${LMMS_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp")
# SDK 3.8's Windows discovery source passes u8string() to std::string APIs.
# Compile that unchanged third-party source as C++17; the host remains C++20.
set_target_properties(LmmsVst3Loader PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(LmmsVst3Loader PRIVATE NOMINMAX)
target_link_libraries(
	LmmsVst3Loader
	PUBLIC sdk_hosting
	PRIVATE ole32 shell32 uuid)

add_library(LmmsVst3Module STATIC Vst3Module.cpp Vst3Module.h Vst3Instance.cpp Vst3Instance.h Vst3Editor.cpp
								  Vst3Editor.h)
target_compile_features(LmmsVst3Module PUBLIC cxx_std_20)
target_compile_definitions(LmmsVst3Module PRIVATE NOMINMAX)
target_include_directories(
	LmmsVst3Module
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
	PRIVATE ../../include)
target_link_libraries(LmmsVst3Module PUBLIC LmmsVst3Loader)

add_executable(RemoteVstHost RemoteVstHost.cpp)
target_include_directories(RemoteVstHost PRIVATE ../../include)
target_compile_definitions(RemoteVstHost PRIVATE NOMINMAX)
target_link_libraries(RemoteVstHost PRIVATE LmmsVst3Module)

include(CTest)
if(BUILD_TESTING)
	add_executable(WaveShellInventory ../../tests/vsthost/WaveShellInventory.cpp)
	target_compile_features(WaveShellInventory PRIVATE cxx_std_20)
	target_compile_definitions(WaveShellInventory PRIVATE NOMINMAX)
	target_include_directories(WaveShellInventory PRIVATE ../../include)
	target_link_libraries(WaveShellInventory PRIVATE LmmsVst3Loader bcrypt)
	foreach(fault RANGE 0 8)
		if(fault EQUAL 0)
			set(fixture Vst3Factory)
		else()
			set(fixture Vst3FactoryFault${fault})
		endif()
		add_library(${fixture} MODULE ../../tests/vsthost/fixtures/Vst3Factory.cpp
									  ../../tests/vsthost/fixtures/Vst3Factory.def)
		target_compile_features(${fixture} PRIVATE cxx_std_17)
		target_compile_definitions(${fixture} PRIVATE VST3_FACTORY_FAULT=${fault} NOMINMAX)
		target_link_libraries(${fixture} PRIVATE pluginterfaces)
		set_target_properties(${fixture} PROPERTIES PREFIX "" SUFFIX ".vst3")
	endforeach()
	add_library(
		Vst3Native MODULE ../../tests/vsthost/fixtures/Vst3Factory.cpp ../../tests/vsthost/fixtures/Vst3Native.cpp
						  ../../tests/vsthost/fixtures/Vst3Factory.def)
	target_compile_definitions(Vst3Native PRIVATE VST3_FACTORY_NATIVE NOMINMAX)
	target_link_libraries(Vst3Native PRIVATE sdk)
	set_target_properties(Vst3Native PROPERTIES PREFIX "" SUFFIX ".vst3")
	add_executable(Vst3RestartEventsTest ../../tests/vsthost/Vst3RestartEventsTest.cpp)
	target_compile_features(Vst3RestartEventsTest PRIVATE cxx_std_20)
	target_compile_definitions(Vst3RestartEventsTest PRIVATE NOMINMAX)
	target_include_directories(Vst3RestartEventsTest PRIVATE ../../include)
	target_link_libraries(Vst3RestartEventsTest PRIVATE LmmsVst3Module)
	add_test(NAME Vst3RestartEvents COMMAND Vst3RestartEventsTest $<TARGET_FILE:Vst3Native>)
	set_tests_properties(Vst3RestartEvents PROPERTIES TIMEOUT 15 LABELS "vsthost;vst3;native;realtime")
	add_executable(Vst3PresetSdkTest ../../tests/vsthost/Vst3PresetSdkTest.cpp
									 "${LMMS_VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
	target_compile_features(Vst3PresetSdkTest PRIVATE cxx_std_20)
	target_compile_definitions(Vst3PresetSdkTest PRIVATE NOMINMAX)
	target_include_directories(Vst3PresetSdkTest PRIVATE ../../include .)
	target_link_libraries(Vst3PresetSdkTest PRIVATE sdk)
	add_test(NAME Vst3PresetSdk COMMAND Vst3PresetSdkTest)
	set_tests_properties(Vst3PresetSdk PROPERTIES TIMEOUT 10 LABELS "vsthost;vst3;preset;sdk")
	add_executable(Vst3CatalogTest ../../tests/vsthost/Vst3CatalogTest.cpp)
	target_compile_features(Vst3CatalogTest PRIVATE cxx_std_20)
	target_compile_definitions(Vst3CatalogTest PRIVATE NOMINMAX)
	target_include_directories(Vst3CatalogTest PRIVATE ../../include)
	target_link_libraries(Vst3CatalogTest PRIVATE bcrypt)
	add_test(NAME Vst3Catalog COMMAND Vst3CatalogTest $<TARGET_FILE:RemoteVstHost> $<TARGET_FILE_DIR:Vst3Factory>)
	set_tests_properties(Vst3Catalog PROPERTIES TIMEOUT 30 LABELS "vsthost;vst3;native;faults")
	add_executable(Vst3ProcessingTest ../../tests/vsthost/Vst3ProcessingTest.cpp)
	target_compile_features(Vst3ProcessingTest PRIVATE cxx_std_20)
	target_compile_definitions(Vst3ProcessingTest PRIVATE NOMINMAX)
	target_include_directories(Vst3ProcessingTest PRIVATE ../../include)
	target_link_libraries(Vst3ProcessingTest PRIVATE bcrypt)
	add_test(NAME Vst3Processing COMMAND Vst3ProcessingTest $<TARGET_FILE:RemoteVstHost> $<TARGET_FILE:Vst3Native>)
	set_tests_properties(Vst3Processing PROPERTIES TIMEOUT 30 LABELS "vsthost;vst3;native;audio")
	add_executable(Vst3LifecycleTest ../../tests/vsthost/Vst3LifecycleTest.cpp)
	target_compile_features(Vst3LifecycleTest PRIVATE cxx_std_20)
	target_compile_definitions(Vst3LifecycleTest PRIVATE NOMINMAX)
	target_include_directories(Vst3LifecycleTest PRIVATE ../../include)
	target_link_libraries(Vst3LifecycleTest PRIVATE bcrypt)
	add_test(NAME Vst3Lifecycle COMMAND Vst3LifecycleTest $<TARGET_FILE:RemoteVstHost> $<TARGET_FILE:Vst3Native>)
	set_tests_properties(Vst3Lifecycle PROPERTIES TIMEOUT 60 LABELS "vsthost;vst3;native;editor;lifecycle")
endif()
