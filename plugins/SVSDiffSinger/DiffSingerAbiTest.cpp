/* SDK-only A0 negotiation test. Empty catalog is intentional, never a fake voice. */
#include "svs.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
int run(const std::filesystem::path& path, const std::filesystem::path& voices)
{
#ifdef _WIN32
    auto library = LoadLibraryExW(std::filesystem::absolute(path).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library) { throw std::runtime_error("Cannot load native DiffSinger library/dependencies: Win32 " + std::to_string(GetLastError())); }
    auto entry = reinterpret_cast<svs_get_api_fn>(GetProcAddress(library, "svs_get_api"));
#else
    auto library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    require(library != nullptr, "Cannot load native DiffSinger library/dependencies");
    auto entry = reinterpret_cast<svs_get_api_fn>(dlsym(library, "svs_get_api"));
#endif
    require(entry != nullptr, "Missing ABI export");
    svs_api api{};
    require(entry(2, 0, sizeof(api), &api) == SVS_BAD_ABI, "Wrong major accepted");
    require(entry(1, 0, SVS_API_REQUIRED_SIZE - 1, &api) == SVS_BAD_ABI, "Undersized table accepted");
    for (uint32_t minor = 0; minor <= SVS_ABI_MINOR; ++minor) {
        struct Guarded { svs_api api; unsigned char guard[16]; } guarded{};
        std::memset(guarded.guard, 0x5a, sizeof(guarded.guard));
        const uint32_t capacity = minor == 0 ? SVS_API_REQUIRED_SIZE : uint32_t(sizeof(svs_api));
        require(entry(1, minor, capacity, &guarded.api) == SVS_OK, "Compatible ABI rejected");
        require(guarded.api.size <= capacity, "API table overran capacity");
        for (auto value : guarded.guard) { require(value == 0x5a, "Output guard overwritten"); }
        svs_engine engine = nullptr;
        require(guarded.api.create_engine(nullptr, &engine) == SVS_OK && engine, "Native engine initialization failed");
        const char* catalog = nullptr;
        require(guarded.api.catalog(engine, &catalog) == SVS_OK && catalog, "Catalog query failed");
        const auto json = nlohmann::json::parse(catalog);
        guarded.api.release_string(engine, catalog);
        require(json.at("voices").is_array() && json.at("voices").empty(), "Bootstrap must not register placeholder voices");
        svs_session session = nullptr;
        require(guarded.api.create_session(engine, "missing", &session) == SVS_INVALID_INPUT && !session, "Missing voice accepted");
        guarded.api.destroy_engine(engine);
        std::cout << "PASS ABI 1." << minor << " bounded negotiation / native CPU init / empty catalog\n";
    }
    {
        svs_sdk::Engine engine(entry);
        require(engine.hasEngineSettings(), "Missing engine declaration");
        const auto settings = nlohmann::json::parse(engine.engineSettings().c_str());
        require(settings.at("name") == "DiffSinger" && settings.at("engineType") == "ai", "Invalid engine declaration");
        require(settings.at("engineSettings").size()==3, "Engine must declare three settings including the display-only prefix option");
        require(engine.hasCatalogQuery(), "Missing optional ABI 1.3 catalog query");
        if (!voices.empty()) {
            const nlohmann::json context{{"rescan",true},{"engineSettings",{{"diffsinger.voicebankDirectories",{std::filesystem::absolute(voices).u8string()}}}}};
            const auto catalog=nlohmann::json::parse(engine.catalog(context.dump().c_str()).c_str());
            require(catalog.at("voices").size()==6,"Native ABI catalog did not discover six voices");
            for(const auto& voice:catalog.at("voices")) {
                const auto declaration=nlohmann::json::parse(engine.capabilities(voice.at("id").get<std::string>().c_str()).c_str());
                const auto& segmented=declaration.at("synthesis").at("segmented");
                require(segmented.at("split")=="rests"&&segmented.at("version")==1&&segmented.at("paddingSeconds")==.65,"Missing rest segmentation contract");
                for(const auto& parameter:declaration.at("parameters")) if(parameter.value("curve",false)) {require(parameter.contains("color")&&parameter.at("color").get<std::string>().size()==7,"Missing curve color");}
            }
            const auto id=catalog.at("voices")[0].at("avatar").get<std::string>();
            svs_api table{};require(entry(1,3,sizeof(table),&table)==SVS_OK,"ABI 1.3 negotiation failed");
            svs_engine raw=nullptr;require(table.create_engine(nullptr,&raw)==SVS_OK,"Resource engine failed");
            const char* result=nullptr;require(table.query_catalog(raw,context.dump().c_str(),&result)==SVS_OK,"Resource catalog failed");table.release_string(raw,result);
            svs_resource resource=nullptr;svs_resource_info info{};info.size=sizeof(info);
            require(table.open_resource(raw,id.c_str(),&resource,&info)==SVS_OK&&resource&&info.byte_count>0,"Cannot open declared image resource");
            require(std::strlen(info.sha256)==64,"Resource digest missing");
            std::vector<unsigned char> bytes(static_cast<size_t>(info.byte_count));uint64_t count=0;
            require(table.query_catalog(raw,R"({"rescan":true,"engineSettings":{"diffsinger.voicebankDirectories":[]}})",&result)==SVS_OK,"Empty rescan failed");table.release_string(raw,result);
            require(table.read_resource(raw,resource,0,bytes.data(),bytes.size(),&count)==SVS_OK&&count==bytes.size(),"Old resource handle did not survive catalog refresh");
            require(table.read_resource(raw,resource,info.byte_count+1,bytes.data(),1,&count)==SVS_INVALID_INPUT,"Out of range resource read accepted");
            table.close_resource(raw,resource);table.destroy_engine(raw);
            std::cout<<"PASS ABI 1.3 six voices / three settings / rest segmentation and curve colors / resource lifetime and bounds\n";
        }
    }
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    if (argc != 2 && argc != 3) { std::cerr << "Usage: DiffSingerAbiTest <plugin library> [voice root]\n"; return 2; }
    try { return run(std::filesystem::path(argv[1]),argc==3?std::filesystem::path(argv[2]):std::filesystem::path{}); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
