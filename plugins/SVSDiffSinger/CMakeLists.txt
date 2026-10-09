cmake_minimum_required(VERSION 3.16)
project(
	SVSDiffSinger
	VERSION 0.1.0
	LANGUAGES CXX)
include(FetchContent)
if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW)
endif()
# yaml-cpp 0.8.0 predates CMake 4's removal of pre-3.5 policy support.
# This is local to the engine directory and its dependencies.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(LMMS_ONNX_ROOT
	"$ENV{LMMS_ONNX_ROOT}"
	CACHE PATH "Native ONNX Runtime 1.23.0 CPU SDK root")
if(NOT EXISTS "${LMMS_ONNX_ROOT}/include/onnxruntime_cxx_api.h" OR NOT EXISTS "${LMMS_ONNX_ROOT}/VERSION_NUMBER")
	message(
		FATAL_ERROR
			"DiffSinger requires native ONNX Runtime CPU 1.23.0. Set LMMS_ONNX_ROOT to the extracted official SDK; no Python/.NET fallback is supported."
	)
endif()
file(READ "${LMMS_ONNX_ROOT}/VERSION_NUMBER" ort_version)
string(STRIP "${ort_version}" ort_version)
if(NOT ort_version STREQUAL "1.23.0")
	message(FATAL_ERROR "DiffSinger requires ONNX Runtime 1.23.0, found ${ort_version}")
endif()
find_library(
	diffsinger_ort_library
	NAMES onnxruntime
	PATHS "${LMMS_ONNX_ROOT}/lib"
	NO_DEFAULT_PATH REQUIRED)
add_library(diffsinger-ort SHARED IMPORTED)
if(WIN32)
	set_target_properties(diffsinger-ort PROPERTIES IMPORTED_IMPLIB "${diffsinger_ort_library}"
													IMPORTED_LOCATION "${LMMS_ONNX_ROOT}/lib/onnxruntime.dll")
else()
	set_target_properties(diffsinger-ort PROPERTIES IMPORTED_LOCATION "${diffsinger_ort_library}")
endif()
set_target_properties(diffsinger-ort PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LMMS_ONNX_ROOT}/include")
set(JSON_BuildTests
	OFF
	CACHE BOOL "" FORCE)
set(JSON_Install
	OFF
	CACHE BOOL "" FORCE)
FetchContent_Declare(
	diffsinger_json
	URL https://github.com/nlohmann/json/archive/55f93686c01528224f448c19128836e7df245f72.tar.gz
	URL_HASH SHA256=67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef)
set(YAML_CPP_BUILD_TESTS
	OFF
	CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS
	OFF
	CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL
	OFF
	CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS
	OFF
	CACHE BOOL "" FORCE)
FetchContent_Declare(
	diffsinger_yaml
	URL https://github.com/jbeder/yaml-cpp/archive/f7320141120f720aecc4c32be25586e7da9eb978.tar.gz
	URL_HASH SHA256=2fd3bf695ccc056835a70dd6d3046312cc1004e8338b8b5c91646918a27b7ef6)
FetchContent_MakeAvailable(diffsinger_json diffsinger_yaml)
if(CMAKE_PROJECT_NAME STREQUAL "lmms")
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../src/core/svs/compute"
		"${CMAKE_BINARY_DIR}/src/core/svs/compute")
endif()
add_library(DiffSingerNative INTERFACE)
target_compile_features(DiffSingerNative INTERFACE cxx_std_17)
target_compile_definitions(DiffSingerNative INTERFACE NOMINMAX)
target_link_libraries(DiffSingerNative INTERFACE diffsinger-ort nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp)
if(MSVC)
	target_compile_options(DiffSingerNative INTERFACE /utf-8)
endif()
if(NOT TARGET SVSSDK::SDK)
	find_package(SVSSDK CONFIG REQUIRED)
endif()
add_library(DiffSingerCatalog STATIC VoiceCatalog.cpp)
set_target_properties(DiffSingerCatalog PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(DiffSingerCatalog PUBLIC DiffSingerNative)
add_library(
	DiffSingerInference STATIC
	NativeRuntime.cpp
	Pronunciation.cpp
	CpuModel.cpp
	Duration.cpp
	Speaker.cpp
	TensorCache.cpp
	Synthesis.cpp)
set_target_properties(DiffSingerInference PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(DiffSingerInference PUBLIC DiffSingerCatalog SVSSDK::SDK ${CMAKE_DL_LIBS})
add_library(SVSDiffSinger MODULE SVSDiffSinger.cpp)
target_link_libraries(SVSDiffSinger PRIVATE DiffSingerInference SVSSDK::SDK)
target_compile_definitions(SVSDiffSinger PRIVATE SVS_PLUGIN_BUILD)
set(diffsinger_output "${CMAKE_BINARY_DIR}/svs/SVSDiffSinger/$<0:unused>")
if(WIN32 AND CMAKE_PROJECT_NAME STREQUAL "lmms")
	set(diffsinger_output "${CMAKE_BINARY_DIR}/$<CONFIG>/svs/SVSDiffSinger")
endif()
set_target_properties(
	SVSDiffSinger
	PROPERTIES PREFIX ""
			   RUNTIME_OUTPUT_DIRECTORY "${diffsinger_output}"
			   LIBRARY_OUTPUT_DIRECTORY "${diffsinger_output}")
string(TOLOWER "${CMAKE_SYSTEM_NAME}" SVS_PLATFORM)
if(SVS_PLATFORM STREQUAL "darwin")
	set(SVS_PLATFORM macos)
endif()
set(SVS_ARCHITECTURE x64)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(SVS_ARCHITECTURE x86)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" OR CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
	set(SVS_ARCHITECTURE arm64)
endif()
configure_file(manifest.json.in "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" @ONLY)
add_custom_command(
	TARGET SVSDiffSinger
	POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/manifest.json"
			"$<TARGET_FILE_DIR:SVSDiffSinger>/manifest.json")
foreach(native_tool DiffSingerModelProbe DiffSingerAbiTest DiffSingerCatalogTest)
	add_executable(${native_tool} "${native_tool}.cpp")
	target_link_libraries(${native_tool} PRIVATE DiffSingerCatalog SVSSDK::SDK ${CMAKE_DL_LIBS})
endforeach()
if(WIN32)
	target_link_libraries(DiffSingerCatalogTest PRIVATE advapi32)
endif()
if(WIN32)
	foreach(native_target SVSDiffSinger DiffSingerModelProbe DiffSingerAbiTest DiffSingerCatalogTest)
		add_custom_command(
			TARGET ${native_target}
			POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LMMS_ONNX_ROOT}/lib/onnxruntime.dll"
					"$<TARGET_FILE_DIR:${native_target}>")
	endforeach()
	install(FILES "${LMMS_ONNX_ROOT}/lib/onnxruntime.dll" DESTINATION svs/SVSDiffSinger)
endif()
install(
	TARGETS SVSDiffSinger
	LIBRARY DESTINATION svs/SVSDiffSinger
	RUNTIME DESTINATION svs/SVSDiffSinger)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" DESTINATION svs/SVSDiffSinger)
install(FILES "${LMMS_ONNX_ROOT}/LICENSE" "${LMMS_ONNX_ROOT}/ThirdPartyNotices.txt"
		DESTINATION svs/SVSDiffSinger/licenses/onnxruntime)
install(FILES "${diffsinger_json_SOURCE_DIR}/LICENSE.MIT" DESTINATION svs/SVSDiffSinger/licenses/json)
install(FILES "${diffsinger_yaml_SOURCE_DIR}/LICENSE" DESTINATION svs/SVSDiffSinger/licenses/yaml-cpp)

# Frozen original MIT datasets, shared by native plugin and standalone consumers.
set(pronunciation_root "${CMAKE_BINARY_DIR}/_deps/diffsinger-pronunciation")
file(MAKE_DIRECTORY "${pronunciation_root}")
function(diffsinger_pronunciation_file name repo revision source hash)
	set(path "${pronunciation_root}/${name}")
	if(NOT EXISTS "${path}")
		file(
			DOWNLOAD "https://raw.githubusercontent.com/mozillazg/${repo}/${revision}/${source}" "${path}"
			EXPECTED_HASH "SHA256=${hash}"
			TLS_VERIFY ON
			STATUS download_status)
		list(GET download_status 0 download_code)
		if(NOT download_code EQUAL 0)
			message(FATAL_ERROR "DiffSinger pronunciation data download failed: ${download_status}")
		endif()
	endif()
	file(SHA256 "${path}" actual_hash)
	if(NOT actual_hash STREQUAL hash)
		message(FATAL_ERROR "DiffSinger pronunciation data hash mismatch: ${name}")
	endif()
endfunction()
diffsinger_pronunciation_file(pinyin.txt pinyin-data 9193766130af24d2ac54230be979b2e98ac66223 pinyin.txt
							  621f8ca9eff8519f47e2b17b564fd318161e13bca07eea8c8e04993cd5d3b52e)
diffsinger_pronunciation_file(phrases.txt phrase-pinyin-data cee0ed6e6e4898580cafd2bd5e3723e20b214aa0 pinyin.txt
							  dcc769607c220b312fea3e71cb63421298b4b891b1f7356a95ab58f2c96fff81)
diffsinger_pronunciation_file(LICENSE-pinyin-data pinyin-data 9193766130af24d2ac54230be979b2e98ac66223 LICENSE
							  9c048697be2502a16e8bcb282d5d465a07295b2def0ffb05a269c5d39dbe1586)
diffsinger_pronunciation_file(LICENSE-phrase-pinyin-data phrase-pinyin-data cee0ed6e6e4898580cafd2bd5e3723e20b214aa0
							  LICENSE 89ac55df747e4776088c3e77531ef61b973a1a59dd8e6a4548a58996da9a4f70)
add_custom_command(
	TARGET SVSDiffSinger
	POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_directory "${pronunciation_root}" "$<TARGET_FILE_DIR:SVSDiffSinger>/data"
	COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses"
			"$<TARGET_FILE_DIR:SVSDiffSinger>/licenses"
	COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:SVSDiffSinger>/licenses/onnxruntime"
			"$<TARGET_FILE_DIR:SVSDiffSinger>/licenses/json" "$<TARGET_FILE_DIR:SVSDiffSinger>/licenses/yaml-cpp"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LMMS_ONNX_ROOT}/LICENSE"
			"${LMMS_ONNX_ROOT}/ThirdPartyNotices.txt" "$<TARGET_FILE_DIR:SVSDiffSinger>/licenses/onnxruntime"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${diffsinger_json_SOURCE_DIR}/LICENSE.MIT"
			"$<TARGET_FILE_DIR:SVSDiffSinger>/licenses/json"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${diffsinger_yaml_SOURCE_DIR}/LICENSE"
			"$<TARGET_FILE_DIR:SVSDiffSinger>/licenses/yaml-cpp")
install(DIRECTORY "${pronunciation_root}/" DESTINATION svs/SVSDiffSinger/data)
install(FILES "licenses/tlds-MIT.txt" DESTINATION svs/SVSDiffSinger/licenses)
add_custom_command(
	TARGET SVSDiffSinger
	POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:SVSDiffSinger>/../vocoders"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/VOCODERS.md"
			"$<TARGET_FILE_DIR:SVSDiffSinger>/../vocoders/README.md")
install(
	FILES "VOCODERS.md"
	DESTINATION svs/vocoders
	RENAME README.md)
add_executable(DiffSingerDurationTest DiffSingerDurationTest.cpp)
add_executable(DiffSingerSynthesisTest DiffSingerSynthesisTest.cpp)
target_link_libraries(DiffSingerSynthesisTest PRIVATE DiffSingerInference)
add_custom_command(
	TARGET DiffSingerSynthesisTest
	POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_directory "${pronunciation_root}"
			"$<TARGET_FILE_DIR:DiffSingerSynthesisTest>/data")
if(WIN32)
	add_custom_command(
		TARGET DiffSingerSynthesisTest
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LMMS_ONNX_ROOT}/lib/onnxruntime.dll"
				"$<TARGET_FILE_DIR:DiffSingerSynthesisTest>")
endif()
target_link_libraries(DiffSingerDurationTest PRIVATE DiffSingerInference)
add_custom_command(
	TARGET DiffSingerDurationTest
	POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_directory "${pronunciation_root}"
			"$<TARGET_FILE_DIR:DiffSingerDurationTest>/data")
if(WIN32)
	add_custom_command(
		TARGET DiffSingerDurationTest
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LMMS_ONNX_ROOT}/lib/onnxruntime.dll"
				"$<TARGET_FILE_DIR:DiffSingerDurationTest>")
endif()
