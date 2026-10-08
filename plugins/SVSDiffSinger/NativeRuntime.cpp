#include "NativeRuntime.h"
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace diffsinger {
std::filesystem::path packageDirectory() {
#ifdef _WIN32
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<const wchar_t*>(&packageDirectory),&module)) {throw std::runtime_error("Cannot locate DiffSinger runtime package");}
    wchar_t filename[32768];const auto length=GetModuleFileNameW(module,filename,32768);
    if(!length||length>=32768) {throw std::runtime_error("Invalid DiffSinger package path");}
    return std::filesystem::path(filename).parent_path();
#else
    Dl_info info{};if(!dladdr(reinterpret_cast<void*>(&packageDirectory),&info)||!info.dli_fname) {throw std::runtime_error("Cannot locate DiffSinger runtime package");}
    return std::filesystem::path(info.dli_fname).parent_path();
#endif
}
void initializeRuntime() {
#ifdef _WIN32
    struct Runtime {
        HMODULE library=nullptr;
        Runtime() {
            const auto path=packageDirectory()/L"onnxruntime.dll";
            library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if(!library) {throw std::runtime_error("Cannot load packaged DiffSinger ONNX Runtime: Win32 "+std::to_string(GetLastError()));}
            using GetBase=const OrtApiBase*(ORT_API_CALL*)();const auto get=reinterpret_cast<GetBase>(GetProcAddress(library,"OrtGetApiBase"));
            const auto base=get?get():nullptr;
            if(!base||std::string(base->GetVersionString())!="1.23.0") {FreeLibrary(library);library=nullptr;throw std::runtime_error("DiffSinger requires packaged ONNX Runtime CPU 1.23.0");}
            const auto api=base->GetApi(ORT_API_VERSION);
            if(!api) {FreeLibrary(library);library=nullptr;throw std::runtime_error("Packaged ORT API is incompatible");}
            Ort::InitApi(api);
        }
        ~Runtime() {if(library) {FreeLibrary(library);}}
    };
    static Runtime runtime;
#else
    if(std::string(OrtGetApiBase()->GetVersionString())!="1.23.0") {throw std::runtime_error("DiffSinger requires ONNX Runtime CPU 1.23.0");}
#endif
}
}
