# Independent native SVS module; no TuneLab/.NET/reference-source dependencies.
add_library(lmmssvs STATIC SVSPlugin.cpp SVSCapabilities.cpp SVSClip.cpp ../../tracks/SVSTrack.cpp ../../gui/editors/svs/SVSViews.cpp ../../gui/editors/svs/SVSParameterPanel.cpp
  "${CMAKE_SOURCE_DIR}/include/SVSClip.h" "${CMAKE_SOURCE_DIR}/include/SVSTrack.h" "${CMAKE_SOURCE_DIR}/include/SVSViews.h")
target_compile_features(lmmssvs PUBLIC cxx_std_20)
target_compile_definitions(lmmssvs PRIVATE LMMS_STATIC_DEFINE NOMINMAX)
target_include_directories(lmmssvs PUBLIC "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/sdk/svs/include" "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/src")
target_link_libraries(lmmssvs PUBLIC ${QT_LIBRARIES})
install(DIRECTORY "${CMAKE_SOURCE_DIR}/sdk/svs/include/" DESTINATION include/svs)
