set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

add_executable(SVSProjectProcessTest svs/SVSProjectProcessTest.cpp
 ../src/core/svs/SVSProjectBridge.cpp ../include/SVSProjectBridge.h)
target_include_directories(SVSProjectProcessTest PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_features(SVSProjectProcessTest PRIVATE cxx_std_20)
target_compile_definitions(SVSProjectProcessTest PRIVATE
 LMMS_SVS_PROJECT_RUNTIME="${CMAKE_BINARY_DIR}/Release/svs-project")
target_link_libraries(SVSProjectProcessTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})
add_test(NAME SVSProjectProcessTest COMMAND SVSProjectProcessTest
 -o "${CMAKE_CURRENT_BINARY_DIR}/SVSProjectProcessTest-results.txt,txt" -o "-,txt")
set_tests_properties(SVSProjectProcessTest PROPERTIES TIMEOUT 90 LABELS "svs;project;M1")

add_executable(SVSProjectMapperTest svs/SVSProjectMapperTest.cpp ../src/core/svs/SVSProjectMapper.cpp)
target_include_directories(SVSProjectMapperTest PRIVATE "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/sdk/svs/include")
target_compile_features(SVSProjectMapperTest PRIVATE cxx_std_20)
target_link_libraries(SVSProjectMapperTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})
add_test(NAME SVSProjectMapperTest COMMAND SVSProjectMapperTest
 -o "${CMAKE_CURRENT_BINARY_DIR}/SVSProjectMapperTest-results.txt,txt" -o "-,txt")
set_tests_properties(SVSProjectMapperTest PROPERTIES TIMEOUT 30 LABELS "svs;project;M2")

set(LMMS_TESTS
	src/core/ArrayVectorTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AutomatableModelTest.cpp
	src/core/MathTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RelativePathsTest.cpp
	vsthost/VstPathsMigrationTest.cpp
	src/core/TimelineTest.cpp
	src/tracks/AutomationTrackTest.cpp
	svs/SVSIntegrationTest.cpp
	ui/UiBaselineCapture.cpp
	ui/ThemeWidgetTest.cpp
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

add_dependencies(SVSIntegrationTest SVSExample)
if(LMMS_BUILD_DIFFSINGER)
 add_dependencies(SVSIntegrationTest SVSDiffSinger)
endif()
add_dependencies(UiBaselineCapture tripleoscillator kicker amplifier SVSExample)
add_dependencies(ThemeWidgetTest tripleoscillator kicker amplifier SVSExample)
set_target_properties(ThemeWidgetTest PROPERTIES OUTPUT_NAME lmms
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/theme/$<CONFIG>"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/theme/$<CONFIG>")
set_tests_properties(ThemeWidgetTest PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
	ENVIRONMENT "QT_QPA_PLATFORM=windows;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/$<CONFIG>/plugins;LMMS_SVS_PLUGIN_DIR=$<TARGET_FILE_DIR:SVSExample>/..;LMMS_UI_EVIDENCE=${CMAKE_BINARY_DIR}/theme-evidence")
# One aggregate lets MSBuild schedule the frozen UI plugin coverage in parallel.
add_custom_target(UiPluginCoverage)
file(STRINGS "${CMAKE_SOURCE_DIR}/doc/ui-modernization/validation/plugin-targets.txt" UI_PLUGIN_TARGETS)
foreach(UI_PLUGIN_TARGET IN LISTS UI_PLUGIN_TARGETS)
	if(TARGET ${UI_PLUGIN_TARGET})
		add_dependencies(UiPluginCoverage ${UI_PLUGIN_TARGET})
	endif()
endforeach()
# Windows plugins import lmms.exe; give the harness that name so they share its Engine.
set_target_properties(UiBaselineCapture PROPERTIES OUTPUT_NAME lmms
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/ui/$<CONFIG>"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/ui/$<CONFIG>")
set_tests_properties(UiBaselineCapture PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
	ENVIRONMENT "QT_QPA_PLATFORM=windows;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/$<CONFIG>/plugins;LMMS_SVS_PLUGIN_DIR=$<TARGET_FILE_DIR:SVSExample>/..;LMMS_UI_EVIDENCE=${CMAKE_BINARY_DIR}/ui-baseline;LMMS_UI_SAMPLE=${CMAKE_SOURCE_DIR}/doc/ui-modernization/fixtures/tone.wav;LMMS_UI_FIXTURE=${CMAKE_BINARY_DIR}/ui-fixture.mmp")
target_include_directories(SVSIntegrationTest PRIVATE "${CMAKE_SOURCE_DIR}/sdk/svs/include")
if(WIN32)
 # Native plugins import lmms.exe; share this harness's initialized Engine.
 set_target_properties(SVSIntegrationTest PROPERTIES OUTPUT_NAME lmms)
endif()
target_compile_definitions(SVSIntegrationTest PRIVATE LMMS_SVS_PROJECT_RUNTIME="${CMAKE_BINARY_DIR}/Release/svs-project")
set_tests_properties(SVSIntegrationTest PROPERTIES TIMEOUT 60 LABELS "svs;M1"
 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
 ENVIRONMENT "LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data;LMMS_SVS_PLUGIN_DIR=$<TARGET_FILE_DIR:SVSExample>/..")
if(WIN32)
 set_property(TEST SVSIntegrationTest APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=windows")
endif()

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
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/vsthost/native/$<CONFIG>"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/vsthost/native/$<CONFIG>")
	add_dependencies(VstEntryPoints vestige vsteffect vstbase Vst2CompatibilityTest)
	if(TARGET RemoteVstHost64)
		add_dependencies(VstEntryPoints RemoteVstHost64)
	endif()
	set_tests_properties(VstEntryPoints PROPERTIES TIMEOUT 120 LABELS "vsthost;baseline"
		ENVIRONMENT "LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_NATIVE_PLUGIN_DIR=$<TARGET_FILE_DIR:vestige>;LMMS_VST_FIXTURE_ROOT=${CMAKE_BINARY_DIR}/vst-s0")
	add_test(NAME VstEntryPointsRealtime COMMAND $<TARGET_FILE:VstEntryPoints> liveGuiAndAudio
		-o "${CMAKE_CURRENT_BINARY_DIR}/VstEntryPointsRealtime-results.txt,txt" -o "-,txt")
	set_tests_properties(VstEntryPointsRealtime PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE LABELS "vsthost;realtime"
		ENVIRONMENT "LMMS_VST_LIVE_GUI=1;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_NATIVE_PLUGIN_DIR=$<TARGET_FILE_DIR:vestige>;LMMS_VST_FIXTURE_ROOT=${CMAKE_BINARY_DIR}/vst-s0")
endif()

if(WANT_AGENT)
	add_executable(AgentHarness src/agent/AgentHarness.cpp)
	target_include_directories(AgentHarness PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
	target_static_libraries(AgentHarness PRIVATE lmmsobjs)
	target_link_libraries(AgentHarness PRIVATE ${QT_LIBRARIES})
	target_compile_features(AgentHarness PRIVATE cxx_std_20)
endif()
