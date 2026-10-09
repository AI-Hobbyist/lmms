cmake_minimum_required(VERSION 3.16)
project(SVSMinimal LANGUAGES C)
if(NOT DEFINED SVS_SDK_INCLUDE)
	find_package(SVSSDK CONFIG QUIET)
	if(NOT TARGET SVSSDK::SDK)
		set(SVS_SDK_INCLUDE
			"${CMAKE_CURRENT_SOURCE_DIR}/../../include"
			CACHE PATH "Public SVS SDK include directory")
	endif()
endif()
add_library(SVSMinimal MODULE minimal.c)
target_include_directories(SVSMinimal PRIVATE "${SVS_SDK_INCLUDE}")
if(TARGET SVSSDK::SDK)
	target_link_libraries(SVSMinimal PRIVATE SVSSDK::SDK)
endif()
target_compile_features(SVSMinimal PRIVATE c_std_11)
if(MSVC)
	target_compile_options(SVSMinimal PRIVATE /experimental:c11atomics)
endif()
target_compile_definitions(SVSMinimal PRIVATE SVS_PLUGIN_BUILD)
set_target_properties(SVSMinimal PROPERTIES PREFIX "")
if(UNIX)
	target_link_libraries(SVSMinimal PRIVATE m)
endif()
string(TOLOWER "${CMAKE_SYSTEM_NAME}" SVS_PLATFORM)
if(SVS_PLATFORM STREQUAL "darwin")
	set(SVS_PLATFORM "macos")
endif()
set(SVS_ARCHITECTURE "x86")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(SVS_ARCHITECTURE "x64")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" OR CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
	set(SVS_ARCHITECTURE "arm64")
endif()
configure_file(manifest.json.in "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" @ONLY)
add_custom_command(
	TARGET SVSMinimal
	POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/manifest.json"
			"$<TARGET_FILE_DIR:SVSMinimal>/manifest.json")
install(
	TARGETS SVSMinimal
	LIBRARY DESTINATION svs/SVSMinimal
	RUNTIME DESTINATION svs/SVSMinimal)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" DESTINATION svs/SVSMinimal)
