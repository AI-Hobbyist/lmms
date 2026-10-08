/* Native DiffSinger CPU engine. A0 bootstrap has no voices or synthesis yet. */
#include "svs.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cstring>
#include <new>
#include <string>

namespace {
struct Engine { Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "DiffSinger"}; };
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
        if (std::string(OrtGetApiBase()->GetVersionString()) != "1.23.0") {
            throw std::runtime_error("DiffSinger requires native ONNX Runtime CPU 1.23.0");
        }
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
    return text(R"({"voices":[],"diagnostics":[{"stage":"bootstrap","message":"DiffSinger A0: discovery and synthesis are not implemented yet"}]})", out);
}
svs_status SVS_CALL settings(svs_engine engine, const char*, const char** out)
{
    if (!engine) { return SVS_INVALID_INPUT; }
    return text(R"({"schemaVersion":1,"name":"DiffSinger","engineType":"ai","engineSettings":[]})", out);
}
svs_status SVS_CALL capabilities(svs_engine, const char*, const char*, const char**) { return SVS_INVALID_INPUT; }
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
    std::memcpy(out, &api, api.size);
    return SVS_OK;
}
