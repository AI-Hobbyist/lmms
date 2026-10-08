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
int run(const std::filesystem::path& path)
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
    if (argc != 2) { std::cerr << "Usage: DiffSingerAbiTest <plugin library>\n"; return 2; }
    try { return run(std::filesystem::path(argv[1])); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
