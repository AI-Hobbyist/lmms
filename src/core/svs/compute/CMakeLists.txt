set(LMMS_ONNX_DML_ROOT "$ENV{LMMS_ONNX_DML_ROOT}" CACHE PATH "Frozen native ORT DirectML 1.23.0 package")
set(LMMS_DIRECTML_ROOT "$ENV{LMMS_DIRECTML_ROOT}" CACHE PATH "Frozen DirectML 1.15.4 package")
add_executable(SVSComputeAbiCheck ComputeAbiCheck.c)
target_link_libraries(SVSComputeAbiCheck PRIVATE SVSSDK::SDK)
set_property(TARGET SVSComputeAbiCheck PROPERTY C_STANDARD 11)
add_executable(SVSComputeConformance ComputeConformance.cpp)
target_compile_features(SVSComputeConformance PRIVATE cxx_std_17)
target_compile_definitions(SVSComputeConformance PRIVATE NOMINMAX)
target_link_libraries(SVSComputeConformance PRIVATE SVSSDK::SDK nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
add_executable(SVSComputeFixtureWriter WriteComputeFixture.cpp)
target_compile_features(SVSComputeFixtureWriter PRIVATE cxx_std_17)
add_library(SVSComputeExample MODULE ComputeExample.cpp)
target_compile_features(SVSComputeExample PRIVATE cxx_std_17)
target_compile_definitions(SVSComputeExample PRIVATE SVS_PLUGIN_BUILD NOMINMAX)
target_link_libraries(SVSComputeExample PRIVATE SVSSDK::SDK nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
set_target_properties(SVSComputeExample PROPERTIES PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/svs/SVSComputeExample"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/svs/SVSComputeExample")
add_dependencies(SVSComputeExample SVSComputeFixtureWriter)
if(WIN32)
	set(svsc_platform windows)
elseif(APPLE)
	set(svsc_platform macos)
else()
	set(svsc_platform linux)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|ARM64|arm64)$")
	set(svsc_architecture arm64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(svsc_architecture x64)
else()
	set(svsc_architecture x86)
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/compute-example-manifest.json"
	"{\"id\":\"org.lmms.svs.compute-test\",\"name\":\"Shared AI Compute Test\",\"version\":\"1.0\",\"category\":\"Singing Voice Synthesis\",\"platform\":\"${svsc_platform}\",\"architecture\":\"${svsc_architecture}\",\"engineType\":\"ai\",\"entry\":\"SVSComputeExample${CMAKE_SHARED_MODULE_SUFFIX}\",\"apiMajor\":1,\"apiMinor\":0}\n")
add_custom_command(TARGET SVSComputeExample POST_BUILD
	COMMAND $<TARGET_FILE:SVSComputeFixtureWriter> "$<TARGET_FILE_DIR:SVSComputeExample>/fixture.onnx"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/compute-example-manifest.json"
		"$<TARGET_FILE_DIR:SVSComputeExample>/manifest.json")
find_package(Threads REQUIRED)
add_library(SVSCompute SHARED ComputeClient.cpp ComputeTransport.cpp)
target_compile_features(SVSCompute PRIVATE cxx_std_17)
target_compile_definitions(SVSCompute PRIVATE SVSC_BUILD NOMINMAX)
target_link_libraries(SVSCompute PRIVATE SVSSDK::SDK nlohmann_json::nlohmann_json Threads::Threads)
target_compile_options(SVSCompute PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
set_target_properties(SVSCompute PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")

add_executable(SVSComputeWorker ComputeWorker.cpp)
target_compile_features(SVSComputeWorker PRIVATE cxx_std_17)
target_compile_definitions(SVSComputeWorker PRIVATE NOMINMAX)
target_compile_options(SVSComputeWorker PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(SVSComputeWorker PRIVATE SVSSDK::SDK nlohmann_json::nlohmann_json Threads::Threads)
set_target_properties(SVSComputeWorker PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/svs/compute")
if(UNIX AND NOT APPLE)
	target_link_libraries(SVSCompute PRIVATE rt)
endif()
if(WIN32 AND LMMS_ONNX_DML_ROOT AND LMMS_DIRECTML_ROOT)
	file(SHA256 "${LMMS_ONNX_DML_ROOT}/runtimes/win-x64/native/onnxruntime.dll" svsc_ort_hash)
	file(SHA256 "${LMMS_DIRECTML_ROOT}/bin/x64-win/DirectML.dll" svsc_dml_hash)
	if(NOT svsc_ort_hash STREQUAL "f5131591edac6b0a8090d0e329040a49319d7a689cb5b465235fbf7030fa8027"
		OR NOT svsc_dml_hash STREQUAL "9c9e6d822561c6c41b90e6994b3e8857cf1d66dbfb1e0c4c799c7c89b4e92da1")
		message(FATAL_ERROR "SVSCompute requires the frozen ORT DML 1.23.0 / DirectML 1.15.4 native DLLs")
	endif()
	target_compile_definitions(SVSCompute PRIVATE SVSC_HAS_DML)
	target_compile_definitions(SVSComputeWorker PRIVATE SVSC_HAS_DML)
	target_include_directories(SVSComputeWorker PRIVATE "${LMMS_ONNX_DML_ROOT}/build/native/include"
		"${LMMS_DIRECTML_ROOT}/include")
	target_link_libraries(SVSComputeWorker PRIVATE d3d12 dxgi
		"${LMMS_ONNX_DML_ROOT}/runtimes/win-x64/native/onnxruntime.lib"
		"${LMMS_DIRECTML_ROOT}/bin/x64-win/DirectML.lib")
	if(MSVC)
		# A CPU worker must start and infer even when the optional GPU runtime is absent.
		target_link_libraries(SVSComputeWorker PRIVATE delayimp)
		target_link_options(SVSComputeWorker PRIVATE
			/DELAYLOAD:DirectML.dll /DELAYLOAD:d3d12.dll /DELAYLOAD:dxgi.dll)
	endif()
	add_custom_command(TARGET SVSComputeWorker POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different
			"${LMMS_ONNX_DML_ROOT}/runtimes/win-x64/native/onnxruntime.dll"
			"${LMMS_ONNX_DML_ROOT}/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
			"${LMMS_DIRECTML_ROOT}/bin/x64-win/DirectML.dll"
			"$<TARGET_FILE_DIR:SVSComputeWorker>")
	add_executable(SVSComputeProbe ComputeProbe.cpp)
	target_compile_features(SVSComputeProbe PRIVATE cxx_std_17)
	target_compile_definitions(SVSComputeProbe PRIVATE NOMINMAX)
	target_compile_options(SVSComputeProbe PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
	target_include_directories(SVSComputeProbe PRIVATE "${LMMS_ONNX_DML_ROOT}/build/native/include"
		"${LMMS_DIRECTML_ROOT}/include")
	target_link_libraries(SVSComputeProbe PRIVATE nlohmann_json::nlohmann_json d3d12 dxgi
		"${LMMS_ONNX_DML_ROOT}/runtimes/win-x64/native/onnxruntime.lib"
		"${LMMS_DIRECTML_ROOT}/bin/x64-win/DirectML.lib")
	set_target_properties(SVSComputeProbe PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/svs/compute")
	add_custom_command(TARGET SVSComputeProbe POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different
			"${LMMS_ONNX_DML_ROOT}/runtimes/win-x64/native/onnxruntime.dll"
			"${LMMS_ONNX_DML_ROOT}/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
			"${LMMS_DIRECTML_ROOT}/bin/x64-win/DirectML.dll"
			"$<TARGET_FILE_DIR:SVSComputeProbe>")
else()
	target_include_directories(SVSComputeWorker PRIVATE "${LMMS_ONNX_ROOT}/include")
	find_library(svsc_cpu_ort NAMES onnxruntime PATHS "${LMMS_ONNX_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
	target_link_libraries(SVSComputeWorker PRIVATE "${svsc_cpu_ort}")
endif()
