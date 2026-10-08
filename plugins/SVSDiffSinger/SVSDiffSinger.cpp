/* Native DiffSinger CPU engine. A1 discovers voices; synthesis arrives in A2/A3. */
#include "svs.h"
#include "VoiceCatalog.h"
#ifdef _WIN32
#define ORT_API_MANUAL_INIT
#include <windows.h>
#endif
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <mutex>

namespace {
using diffsinger::Json;
#ifdef _WIN32
// Windows may already have its system ORT loaded for unrelated OS services.
// Bind this plugin's C++ wrapper to its explicitly packaged API table.
struct NativeRuntime {
    HMODULE library=nullptr;
    NativeRuntime() {
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<const wchar_t*>(&runtime),&module)) {throw std::runtime_error("Cannot locate DiffSinger runtime package");}
        wchar_t filename[32768];const auto length=GetModuleFileNameW(module,filename,32768);
        if(!length||length>=32768) {throw std::runtime_error("Invalid DiffSinger package path");}
        const auto path=std::filesystem::path(filename).parent_path()/L"onnxruntime.dll";
        library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!library) {throw std::runtime_error("Cannot load packaged DiffSinger ONNX Runtime: Win32 "+std::to_string(GetLastError()));}
        using GetBase=const OrtApiBase*(ORT_API_CALL*)();const auto get=reinterpret_cast<GetBase>(GetProcAddress(library,"OrtGetApiBase"));
        const auto base=get?get():nullptr;
        if(!base||std::string(base->GetVersionString())!="1.23.0") {FreeLibrary(library);library=nullptr;throw std::runtime_error("DiffSinger requires packaged ONNX Runtime CPU 1.23.0");}
        const auto api=base->GetApi(ORT_API_VERSION);if(!api) {FreeLibrary(library);library=nullptr;throw std::runtime_error("Packaged ORT API is incompatible");}Ort::InitApi(api);
    }
    ~NativeRuntime() {if(library) FreeLibrary(library);}
    static void runtime() {static NativeRuntime value;}
};
#endif
struct Engine {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "DiffSinger"};
    std::shared_ptr<const diffsinger::Catalog> catalog=std::make_shared<diffsinger::Catalog>();
    std::mutex scanMutex;
    Json scanContext;
};
struct ResourceHandle {std::shared_ptr<const diffsinger::Resource> resource;};
svs_status text(const char* value, const char** out)
{
    if (!out) { return SVS_INVALID_INPUT; }
    *out = nullptr;
    const auto size = std::strlen(value) + 1;
    auto* result = new (std::nothrow) char[size];
    if (!result) { return SVS_FAILED; }
    std::memcpy(result, value, size);
    *out = result;
    return SVS_OK;
}
svs_status SVS_CALL create(const svs_host* host, svs_engine* out)
{
    if (!out) { return SVS_INVALID_INPUT; }
    *out = nullptr;
    try {
#ifdef _WIN32
        NativeRuntime::runtime();
#else
        if (std::string(OrtGetApiBase()->GetVersionString()) != "1.23.0") {
            throw std::runtime_error("DiffSinger requires native ONNX Runtime CPU 1.23.0");
        }
#endif
        *out = new Engine;
        return SVS_OK;
    } catch (const std::exception& error) {
        if (host && SVS_HAS_FIELD(*host, svs_host, log) && host->log) {
            host->log(host->context, 2, error.what());
        }
        return SVS_FAILED;
    }
}
void SVS_CALL destroy(svs_engine engine) { delete static_cast<Engine*>(engine); }
svs_status SVS_CALL catalog(svs_engine engine, const char** out)
{
    if (!engine) { return SVS_INVALID_INPUT; }
    try {return text(std::atomic_load(&static_cast<Engine*>(engine)->catalog)->declaration().dump().c_str(),out);}catch(...) {return SVS_FAILED;}
}
svs_status SVS_CALL queryCatalog(svs_engine handle,const char* request,const char** out)
{
    if(!handle||!out) {return SVS_INVALID_INPUT;}*out=nullptr;
    try {
        auto& engine=*static_cast<Engine*>(handle);const auto context=Json::parse(request?request:"{}");
        if(!context.is_object()) {throw std::runtime_error("Catalog context must be an object");}
        std::lock_guard lock(engine.scanMutex);auto current=std::atomic_load(&engine.catalog);
        const auto settings=context.value("engineSettings",Json::object());
        if(!settings.is_object()) {throw std::runtime_error("Engine settings must be an object");}
        const auto steps=settings.value("diffsinger.renderSteps",Json(20));
        if(!steps.is_number_integer()||steps.get<int64_t>()<1||steps.get<int64_t>()>100) {throw std::runtime_error("Rendering steps must be an integer in 1-100");}
        const auto directories=settings.value("diffsinger.voicebankDirectories",Json::array());
        if(current->revision==0||context.value("rescan",false)||engine.scanContext.value("directories",Json())!=directories) {
            auto next=diffsinger::scan(context,current->revision+1);std::atomic_store(&engine.catalog,next);engine.scanContext={{"directories",directories}};current=std::move(next);
        }
        return text(current->declaration().dump().c_str(),out);
    }catch(const std::exception& error) {text(Json{{"message",error.what()},{"stage","catalog"}}.dump().c_str(),out);return SVS_INVALID_INPUT;}
}
svs_status SVS_CALL settings(svs_engine engine, const char*, const char** out)
{
    if (!engine) { return SVS_INVALID_INPUT; }
    try {return text(diffsinger::engineSettings().dump().c_str(),out);}catch(...) {return SVS_FAILED;}
}
svs_status SVS_CALL capabilities(svs_engine handle, const char* id, const char*, const char** out) {
    if(!handle||!id||!out) {return SVS_INVALID_INPUT;}
    try {const auto voice=std::atomic_load(&static_cast<Engine*>(handle)->catalog)->find(id);if(!voice) {return SVS_INVALID_INPUT;}
        const auto info=voice->declaration();auto phonemes=Json::array();for(auto item=voice->stages.at("acoustic").phonemes.begin();item!=voice->stages.at("acoustic").phonemes.end();++item) {phonemes.push_back(item.key());}
        const Json schema{{"schemaVersion",1},{"languages",info["languages"]},{"defaultLanguage",info["defaultLanguage"]},{"noteLanguage",info["languages"].size()>1},
            {"parameters",Json::array()},{"feedbackParameters",Json::array()},{"pronunciation",{{"phonemeSet","diffsinger:"+voice->fingerprint},{"phonemes",phonemes}}},
            {"synthesis",{{"available",false},{"reason","CPU synthesis is implemented in A2/A3"},{"channels",2},{"format","float32"}}}};
        return text(schema.dump().c_str(),out);
    }catch(...) {return SVS_FAILED;}
}
svs_status SVS_CALL openResource(svs_engine handle,const char* id,svs_resource* out,svs_resource_info* info) {
    if(!handle||!id||!out||!info||info->size<sizeof(*info)) {return SVS_INVALID_INPUT;}*out=nullptr;
    try {const auto catalog=std::atomic_load(&static_cast<Engine*>(handle)->catalog);for(const auto& voice:catalog->voices) {for(const auto& entry:voice->resources) {const auto& resource=entry.second;if(resource->id!=id) {continue;}auto result=std::make_unique<ResourceHandle>();result->resource=resource;*info={sizeof(*info),resource->id.c_str(),resource->mime.c_str(),resource->bytes.size(),resource->sha256.c_str()};*out=result.release();return SVS_OK;}}return SVS_INVALID_INPUT;}catch(...) {return SVS_FAILED;}
}
svs_status SVS_CALL readResource(svs_engine engine,svs_resource handle,uint64_t offset,void* destination,uint64_t capacity,uint64_t* count) {
    if(!engine||!handle||!count||(!destination&&capacity)) {return SVS_INVALID_INPUT;}*count=0;const auto& bytes=static_cast<ResourceHandle*>(handle)->resource->bytes;if(offset>bytes.size()) {return SVS_INVALID_INPUT;}const auto size=std::min(capacity,uint64_t(bytes.size()-offset));if(size) {std::memcpy(destination,bytes.data()+offset,size);}*count=size;return SVS_OK;
}
void SVS_CALL closeResource(svs_engine,svs_resource handle) {delete static_cast<ResourceHandle*>(handle);}
void SVS_CALL releaseString(svs_engine, const char* value) { delete[] value; }
svs_status SVS_CALL createSession(svs_engine, const char*, svs_session* out)
{
    if (out) { *out = nullptr; }
    return SVS_INVALID_INPUT;
}
void SVS_CALL destroySession(svs_session) {}
svs_status SVS_CALL submit(svs_session, const svs_snapshot*) { return SVS_INVALID_INPUT; }
svs_status SVS_CALL render(svs_session, svs_result*) { return SVS_UNSUPPORTED; }
void SVS_CALL cancel(svs_session) {}
void SVS_CALL releaseResult(svs_session, svs_result*) {}
}
SVS_EXPORT svs_status SVS_CALL svs_get_api(uint32_t major, uint32_t minor, uint32_t size, svs_api* out)
{
    if (major != SVS_ABI_MAJOR || !out || size < SVS_API_REQUIRED_SIZE) { return SVS_BAD_ABI; }
    svs_api api{};
    api.major = SVS_ABI_MAJOR;
    api.minor = std::min(minor, uint32_t(SVS_ABI_MINOR));
    api.size = std::min(size, uint32_t(sizeof(api)));
    api.create_engine = create; api.destroy_engine = destroy;
    api.catalog = catalog; api.capabilities = capabilities; api.release_string = releaseString;
    api.create_session = createSession; api.destroy_session = destroySession;
    api.submit = submit; api.render = render; api.cancel = cancel; api.release_result = releaseResult;
    if (SVS_HAS_FIELD(api, svs_api, query_engine_settings) && minor >= 2) {
        api.features |= SVS_FEATURE_ENGINE_SETTINGS;
        api.query_engine_settings = settings;
    }
    if(SVS_HAS_FIELD(api,svs_api,query_catalog)&&minor>=3) {api.features|=SVS_FEATURE_CATALOG_QUERY;api.query_catalog=queryCatalog;}
    if(SVS_HAS_FIELD(api,svs_api,close_resource)&&minor>=1) {api.features|=SVS_FEATURE_RESOURCES;api.open_resource=openResource;api.read_resource=readResource;api.close_resource=closeResource;}
    std::memcpy(out, &api, api.size);
    return SVS_OK;
}
