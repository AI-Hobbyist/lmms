cmake_minimum_required(VERSION 3.16)
project(SVCSDK VERSION 1.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(svc-sdk STATIC src/Stream.cpp src/Capabilities.cpp)
add_library(SVCSDK::SDK ALIAS svc-sdk)
set_target_properties(svc-sdk PROPERTIES EXPORT_NAME SDK POSITION_INDEPENDENT_CODE ON)
target_compile_features(svc-sdk PUBLIC cxx_std_17)
target_include_directories(svc-sdk PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/svc>")
target_link_libraries(svc-sdk PRIVATE Qt6::Core)

add_library(svc-reference STATIC examples/Reference.c)
target_link_libraries(svc-reference PUBLIC svc-sdk)
if(UNIX)
    target_link_libraries(svc-reference PRIVATE m)
endif()
add_executable(SVCConsumer examples/Consumer.c)
target_link_libraries(SVCConsumer PRIVATE svc-reference)
add_executable(SVCContractTest tests/ContractTest.cpp)
target_link_libraries(SVCContractTest PRIVATE svc-reference Qt6::Core)
if(BUILD_TESTING)
    add_test(NAME SVCContractTest COMMAND SVCContractTest)
    add_test(NAME SVCConsumer COMMAND SVCConsumer)
    set_tests_properties(SVCContractTest SVCConsumer PROPERTIES TIMEOUT 30 LABELS "svc;M0")
endif()

install(TARGETS svc-sdk EXPORT SVCSDKTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT SVCSDKTargets NAMESPACE SVCSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SVCSDK)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/svc)
install(DIRECTORY docs/ examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/svc-sdk)
configure_package_config_file(cmake/SVCSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/SVCSDKConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SVCSDK)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/SVCSDKConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SVCSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SVCSDKConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SVCSDK)
