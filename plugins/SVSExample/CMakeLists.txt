cmake_minimum_required(VERSION 3.13)
project(SVSExample LANGUAGES CXX)
set_property(
	DIRECTORY
	APPEND
	PROPERTY CMAKE_CONFIGURE_DEPENDS
			 full.json
			 minimal.json
			 avatar.svg
			 portrait.svg
			 avatar-lite.svg
			 portrait-lite.svg)
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
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/full.json" SVS_FULL_SCHEMA)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/minimal.json" SVS_MINIMAL_SCHEMA)
foreach(resource avatar portrait avatar-lite portrait-lite)
	string(REPLACE "-" "_" symbol "${resource}")
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${resource}.svg" "SVS_${symbol}")
	string(REPLACE "\r\n" "\n" "SVS_${symbol}" "${SVS_${symbol}}")
	string(SHA256 "SVS_${symbol}_HASH" "${SVS_${symbol}}")
endforeach()
configure_file(Schemas.h.in "${CMAKE_CURRENT_BINARY_DIR}/Schemas.h" @ONLY)
if(NOT DEFINED SVS_SDK_INCLUDE)
	find_package(SVSSDK CONFIG QUIET)
	if(NOT TARGET SVSSDK::SDK)
		set(SVS_SDK_INCLUDE
			"${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/svs/include"
			CACHE PATH "Installed SVS SDK include directory")
	endif()
endif()
add_library(SVSExample MODULE SVSExample.cpp)
target_include_directories(SVSExample PRIVATE "${SVS_SDK_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}")
if(TARGET SVSSDK::SDK)
	target_link_libraries(SVSExample PRIVATE SVSSDK::SDK)
endif()
target_compile_features(SVSExample PRIVATE cxx_std_17)
target_compile_definitions(SVSExample PRIVATE SVS_PLUGIN_BUILD)
set(SVS_EXAMPLE_OUTPUT "${CMAKE_BINARY_DIR}/svs/SVSExample/$<0:unused>")
if(WIN32 AND CMAKE_PROJECT_NAME STREQUAL "lmms")
	set(SVS_EXAMPLE_OUTPUT "${CMAKE_BINARY_DIR}/$<CONFIG>/svs/SVSExample")
endif()
set_target_properties(
	SVSExample
	PROPERTIES PREFIX ""
			   RUNTIME_OUTPUT_DIRECTORY "${SVS_EXAMPLE_OUTPUT}"
			   LIBRARY_OUTPUT_DIRECTORY "${SVS_EXAMPLE_OUTPUT}")
add_custom_command(
	TARGET SVSExample
	POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/manifest.json"
			"$<TARGET_FILE_DIR:SVSExample>/manifest.json")
foreach(
	resource
	avatar.svg
	portrait.svg
	avatar-lite.svg
	portrait-lite.svg
	zh.json
	ja.json
	en.json)
	add_custom_command(
		TARGET SVSExample
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${resource}"
				"$<TARGET_FILE_DIR:SVSExample>/${resource}")
endforeach()
install(
	TARGETS SVSExample
	LIBRARY DESTINATION svs/SVSExample
	RUNTIME DESTINATION svs/SVSExample)
install(
	FILES "${CMAKE_CURRENT_BINARY_DIR}/manifest.json"
		  avatar.svg
		  portrait.svg
		  avatar-lite.svg
		  portrait-lite.svg
		  zh.json
		  ja.json
		  en.json
	DESTINATION svs/SVSExample)
install(FILES SVSExample-demo.mmp DESTINATION svs/SVSExample)
