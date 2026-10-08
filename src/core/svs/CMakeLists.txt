# Independent native SVS module; no TuneLab/.NET/reference-source dependencies.
add_library(lmmssvs STATIC SVSPlugin.cpp SVSCapabilities.cpp SVSCurve.cpp SVSWaveform.cpp SVSCache.cpp SVSExportSnapshot.cpp SVSTempoSnapshot.cpp SVSTempoSource.cpp "${CMAKE_SOURCE_DIR}/include/SVSTempoSource.h" SVSSynthesisScheduler.cpp SVSClip.cpp ../../tracks/SVSTrack.cpp ../../gui/editors/svs/SVSViews.cpp ../../gui/editors/svs/SVSParameterPanel.cpp ../../gui/editors/svs/SVSCanvas.cpp
  SVSSegmentedSynthesis.cpp
  ../../gui/editors/svs/SVSLyricEditor.cpp
  ../../gui/editors/svs/SVSImageLoader.cpp
  ../../gui/editors/svs/SVSSettingsPage.cpp
  ../../gui/editors/svs/SVSResultStrip.cpp "${CMAKE_SOURCE_DIR}/include/SVSResultStrip.h"
  "${CMAKE_SOURCE_DIR}/include/SVSClip.h" "${CMAKE_SOURCE_DIR}/include/SVSTrack.h" "${CMAKE_SOURCE_DIR}/include/SVSViews.h" "${CMAKE_SOURCE_DIR}/include/SVSCanvas.h")
target_compile_features(lmmssvs PUBLIC cxx_std_20)
target_sources(lmmssvs PRIVATE "${CMAKE_SOURCE_DIR}/include/SVSModel.h")
target_compile_definitions(lmmssvs PRIVATE LMMS_STATIC_DEFINE NOMINMAX)
target_include_directories(lmmssvs PUBLIC "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/sdk/svs/include" "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/src")
target_link_libraries(lmmssvs PUBLIC ${QT_LIBRARIES})
install(DIRECTORY "${CMAKE_SOURCE_DIR}/sdk/svs/include/" DESTINATION include/svs)
