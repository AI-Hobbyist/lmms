find_package(Qt6 REQUIRED COMPONENTS Network)
add_library(SVCUSVC MODULE USVC.cpp ../SVCRVC/Http.cpp)
target_link_libraries(SVCUSVC PRIVATE svc-sdk Qt6::Core Qt6::Network Qt6::Widgets Qt6::Xml)
target_compile_features(SVCUSVC PRIVATE cxx_std_17)
target_compile_definitions(SVCUSVC PRIVATE SVC_LMMS_MODULE)
target_include_directories(SVCUSVC PRIVATE "${CMAKE_SOURCE_DIR}/include" "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/src")
set_target_properties(SVCUSVC PROPERTIES OUTPUT_NAME svcusvc PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins")
install(TARGETS SVCUSVC LIBRARY DESTINATION plugins RUNTIME DESTINATION plugins)

add_executable(SVCUSVCTest "${CMAKE_SOURCE_DIR}/tests/svc/SVCUSVCTest.cpp")
target_link_libraries(SVCUSVCTest PRIVATE svc-sdk Qt6::Core Qt6::Network)
target_compile_features(SVCUSVCTest PRIVATE cxx_std_17)
add_dependencies(SVCUSVCTest SVCUSVC)
add_test(NAME SVCUSVCTest COMMAND SVCUSVCTest "$<TARGET_FILE:SVCUSVC>")
set_tests_properties(SVCUSVCTest PROPERTIES TIMEOUT 90 LABELS "svc;usvc")
