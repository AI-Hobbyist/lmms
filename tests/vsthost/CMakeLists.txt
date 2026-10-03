cmake_minimum_required(VERSION 3.13)
project(LmmsVstHostContracts LANGUAGES CXX)
include(CTest)
add_executable(VstHostProtocol ProtocolTest.cpp)
target_compile_features(VstHostProtocol PRIVATE cxx_std_20)
target_include_directories(VstHostProtocol PRIVATE ../../include)
add_test(NAME VstHostProtocol COMMAND VstHostProtocol)
add_subdirectory(fixtures)

if(WIN32 AND TARGET Vst2CompatibilityTest)
	include(ExternalProject)
	set(VST_BASELINE_ROOT "${CMAKE_BINARY_DIR}/vst-s0")
	set_target_properties(Vst2Baseline PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${VST_BASELINE_ROOT}/x64/fixtures/$<CONFIG>"
		LIBRARY_OUTPUT_DIRECTORY "${VST_BASELINE_ROOT}/x64/fixtures/$<CONFIG>")
	ExternalProject_Add(VstBaselineX86
		SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
		BINARY_DIR "${VST_BASELINE_ROOT}/x86"
		CMAKE_GENERATOR "${CMAKE_GENERATOR}"
		CMAKE_GENERATOR_PLATFORM Win32
		INSTALL_COMMAND ""
		BUILD_ALWAYS ON)
	add_dependencies(Vst2CompatibilityTest Vst2Baseline VstBaselineX86 RemoteVstPlugin32 RemoteVstPlugin64)
	add_test(NAME VstHostProtocolX86
		COMMAND "${VST_BASELINE_ROOT}/x86/$<CONFIG>/VstHostProtocol.exe")
	set_tests_properties(VstHostProtocol VstHostProtocolX86 PROPERTIES LABELS "vsthost;baseline" TIMEOUT 10)
endif()
