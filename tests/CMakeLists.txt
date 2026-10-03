set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(LMMS_TESTS
	src/core/ArrayVectorTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AutomatableModelTest.cpp
	src/core/MathTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RelativePathsTest.cpp
	src/core/TimelineTest.cpp
	src/tracks/AutomationTrackTest.cpp
)

if(WIN32 AND LMMS_HAVE_VST)
	list(APPEND LMMS_TESTS vsthost/Vst2CompatibilityTest.cpp)
	if(WANT_AGENT)
		list(APPEND LMMS_TESTS vsthost/VstEntryPoints.cpp)
	endif()
endif()

if(WANT_AGENT)
	list(APPEND LMMS_TESTS
		src/agent/CommandBusTest.cpp
		src/agent/CoreCommandsTest.cpp
		src/agent/MixAutomationCommandsTest.cpp
		src/agent/A3CommandsTest.cpp
		src/agent/A4CommandsTest.cpp
	)
endif()

if(WANT_AGENT_MCP)
	list(APPEND LMMS_TESTS src/agent/mcp/McpServerLifecycleTest.cpp src/agent/mcp/McpProtocolTest.cpp)
	list(APPEND LMMS_TESTS src/agent/mcp/McpSecurityTest.cpp src/agent/mcp/McpQueueTest.cpp)
	list(APPEND LMMS_TESTS src/agent/mcp/McpExportLifecycleTest.cpp)
endif()

foreach(LMMS_TEST_SRC IN LISTS LMMS_TESTS)
	# TODO CMake 3.20: Use cmake_path
	get_filename_component(LMMS_TEST_NAME ${LMMS_TEST_SRC} NAME_WE)

	add_executable(${LMMS_TEST_NAME} ${LMMS_TEST_SRC})
	# Keep QtTest diagnostics when Windows does not forward its output to CTest.
	add_test(NAME ${LMMS_TEST_NAME} COMMAND ${LMMS_TEST_NAME}
		-o "${CMAKE_CURRENT_BINARY_DIR}/${LMMS_TEST_NAME}-results.txt,txt" -o "-,txt")
	set_tests_properties(${LMMS_TEST_NAME} PROPERTIES ENVIRONMENT "LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data")

	# TODO CMake 3.12: Propagate usage requirements by linking to lmmsobjs
	target_include_directories(${LMMS_TEST_NAME} PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)

	target_static_libraries("${LMMS_TEST_NAME}" PRIVATE lmmsobjs)
	target_link_libraries(${LMMS_TEST_NAME} PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)

	target_compile_features(${LMMS_TEST_NAME} PRIVATE cxx_std_20)
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE LMMS_TESTING)
endforeach()

if(TARGET Vst2CompatibilityTest)
	# Compile the existing proxy into the test executable so Engine's static
	# state is shared with the proxy, just as in the application.
	target_sources(Vst2CompatibilityTest PRIVATE
		../plugins/VstBase/VstPlugin.cpp ../plugins/VstBase/VstPlugin.h)
	target_include_directories(Vst2CompatibilityTest PRIVATE
		../plugins/VstBase "${CMAKE_BINARY_DIR}/plugins/VstBase/vstbase")
	target_compile_definitions(Vst2CompatibilityTest PRIVATE VSTBASE_STATIC_DEFINE
		REMOTE_VST_PLUGIN_FILEPATH_32="32/RemoteVstPlugin32"
		REMOTE_VST_PLUGIN_FILEPATH_64="RemoteVstPlugin64")
	set_tests_properties(Vst2CompatibilityTest PROPERTIES TIMEOUT 90 LABELS "vsthost;baseline"
		ENVIRONMENT "LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_VST_FIXTURE_ROOT=${CMAKE_BINARY_DIR}/vst-s0")
endif()

add_subdirectory(vsthost)

if(TARGET VstEntryPoints)
	set_target_properties(VstEntryPoints PROPERTIES OUTPUT_NAME lmms
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/vsthost/native/$<CONFIG>")
	add_dependencies(VstEntryPoints vestige vsteffect vstbase Vst2CompatibilityTest)
	set_tests_properties(VstEntryPoints PROPERTIES TIMEOUT 120 LABELS "vsthost;baseline"
		ENVIRONMENT "LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_NATIVE_PLUGIN_DIR=$<TARGET_FILE_DIR:vestige>;LMMS_VST_FIXTURE_ROOT=${CMAKE_BINARY_DIR}/vst-s0")
endif()

if(WANT_AGENT)
	add_executable(AgentHarness src/agent/AgentHarness.cpp)
	target_include_directories(AgentHarness PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
	target_static_libraries(AgentHarness PRIVATE lmmsobjs)
	target_link_libraries(AgentHarness PRIVATE ${QT_LIBRARIES})
	target_compile_features(AgentHarness PRIVATE cxx_std_20)
endif()
