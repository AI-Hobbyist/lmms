#ifndef DIFFSINGER_NATIVE_RUNTIME_H
#define DIFFSINGER_NATIVE_RUNTIME_H
#ifdef _WIN32
#ifndef ORT_API_MANUAL_INIT
#define ORT_API_MANUAL_INIT
#endif
#endif
#include <onnxruntime_cxx_api.h>
#include <filesystem>
namespace diffsinger {
// Bind every inference translation unit to the API from this native package.
void initializeRuntime();
std::filesystem::path packageDirectory();
}
#endif
