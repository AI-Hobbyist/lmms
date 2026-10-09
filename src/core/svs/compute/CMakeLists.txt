set(LMMS_ONNX_DML_ROOT "$ENV{LMMS_ONNX_DML_ROOT}" CACHE PATH "Frozen native ORT DirectML 1.23.0 package")
set(LMMS_DIRECTML_ROOT "$ENV{LMMS_DIRECTML_ROOT}" CACHE PATH "Frozen DirectML 1.15.4 package")
add_executable(SVSComputeAbiCheck ComputeAbiCheck.c)
target_link_libraries(SVSComputeAbiCheck PRIVATE SVSSDK::SDK)
set_property(TARGET SVSComputeAbiCheck PROPERTY C_STANDARD 11)
if(WIN32 AND LMMS_ONNX_DML_ROOT AND LMMS_DIRECTML_ROOT)
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
endif()
