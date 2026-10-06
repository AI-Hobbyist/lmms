/* SVS SDK ABI 1.2. Public C ABI; no LMMS, Qt or C++ runtime types at this boundary. */
#ifndef SVS_API_H
#define SVS_API_H
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#define SVS_CALL __cdecl
#ifdef SVS_PLUGIN_BUILD
#define SVS_EXPORT __declspec(dllexport)
#else
#define SVS_EXPORT
#endif
#else
#define SVS_CALL
#define SVS_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define SVS_ABI_MAJOR 1u
#define SVS_ABI_MINOR 2u
typedef void* svs_engine;
typedef void* svs_session;
typedef void* svs_resource;
typedef int32_t svs_status;
enum { SVS_OK=0, SVS_BAD_ABI=1, SVS_INVALID_INPUT=2, SVS_CANCELLED=3, SVS_FAILED=4, SVS_UNSUPPORTED=5 };
/* Feature bits describe optional ABI functions, not a voice's editable capabilities. */
#define SVS_FEATURE_PRONUNCIATION UINT64_C(1)
#define SVS_FEATURE_RESOURCES UINT64_C(2)
#define SVS_FEATURE_RANGES UINT64_C(4)
#define SVS_FEATURE_HOST_BUFFERS UINT64_C(8)
#define SVS_FEATURE_ENGINE_SETTINGS UINT64_C(16)
#define SVS_ENGINE_TYPE_AI "ai"
#define SVS_ENGINE_TYPE_CONCATENATIVE "concatenative"
#define SVS_ENGINE_TYPE_EXAMPLE "example"
#define SVS_COMPUTE_CPU "cpu"
#define SVS_COMPUTE_DIRECTML "directml"
#define SVS_COMPUTE_LIBTORCH "libtorch"
#define SVS_COMPUTE_VULKAN "vulkan"
typedef struct svs_buffer {
    uint32_t size;
    void* data;
    uint64_t byte_count;
    void* owner;
} svs_buffer;
enum { SVS_BUFFER_RESOURCE=0, SVS_BUFFER_AUDIO=1 };
/* Descriptor strings remain valid until close_resource; SHA-256 is lowercase hex. */
typedef struct svs_resource_info {
    uint32_t size;
    const char* id;
    const char* content_type;
    uint64_t byte_count;
    const char* sha256;
} svs_resource_info;
typedef struct svs_note {
    uint32_t size;
    const char* id;
    double tick, duration_tick, start_seconds, duration_seconds, pitch;
    const char* lyric;
    const char* language;
    const char* pronunciation;
    const char* phonemes_json;
    const char* parameters_json;
} svs_note;
typedef struct svs_snapshot {
    uint32_t size;
    const char* clip_id;
    uint64_t generation, revision, request_id;
    const char* voice_id;
    uint32_t sample_rate;
    const svs_note* notes;
    uint32_t note_count;
    double duration_seconds;
    /* Complete immutable input including tempo, curves, dictionaries, versions. */
    const char* input_json;
} svs_snapshot;
typedef struct svs_result {
    uint32_t size;
    uint32_t sample_rate, channels;
    uint64_t frame_count;
    double start_seconds;
    const float* audio; /* interleaved; owned by plugin until release_result */
    const char* feedback_json;
    const char* error_json;
    void* owner;
} svs_result;
typedef struct svs_host {
    uint32_t size;
    void* context;
    void (SVS_CALL *log)(void*, int32_t, const char*);
    /* Completion delivery must enqueue; never call GUI from a worker. */
    void (SVS_CALL *progress)(void*, uint64_t, double, const char*);
    /* Optional 1.1 tail. Buffers must return to this host's release_buffer. */
    svs_status (SVS_CALL *allocate_buffer)(void*, uint32_t kind, uint64_t bytes, svs_buffer*);
    void (SVS_CALL *release_buffer)(void*, svs_buffer*);
    /* Host copies the UTF-8 diagnostic and enqueues delivery before returning. */
    void (SVS_CALL *completed)(void*, uint64_t request_id, svs_status, const char* diagnostic_json);
} svs_host;
typedef struct svs_api {
    uint32_t size, major, minor;
    uint64_t features;
    svs_status (SVS_CALL *create_engine)(const svs_host*, svs_engine*);
    void (SVS_CALL *destroy_engine)(svs_engine);
    /* Strings below are plugin-owned until release_string. Inputs borrowed for call only. */
    svs_status (SVS_CALL *catalog)(svs_engine, const char**);
    svs_status (SVS_CALL *capabilities)(svs_engine, const char*, const char*, const char**);
    void (SVS_CALL *release_string)(svs_engine, const char*);
    svs_status (SVS_CALL *create_session)(svs_engine, const char*, svs_session*);
    void (SVS_CALL *destroy_session)(svs_session);
    /* submit copies all input before returning. render is called on host worker. */
    svs_status (SVS_CALL *submit)(svs_session, const svs_snapshot*);
    svs_status (SVS_CALL *render)(svs_session, svs_result*);
    void (SVS_CALL *cancel)(svs_session); /* thread-safe request; render must still exit */
    void (SVS_CALL *release_result)(svs_session, svs_result*);
    /* Optional tail: UTF-8 JSON request/result. Returned text uses release_string. */
    svs_status (SVS_CALL *pronunciation)(svs_engine, const char* voice_id, const char* request_json, const char** result_json);
    /* Resource IDs come from the catalog/manifest; these functions never accept paths. */
    svs_status (SVS_CALL *open_resource)(svs_engine, const char* id, svs_resource*, svs_resource_info*);
    svs_status (SVS_CALL *read_resource)(svs_engine, svs_resource, uint64_t offset, void* destination, uint64_t capacity, uint64_t* bytes_read);
    void (SVS_CALL *close_resource)(svs_engine, svs_resource);
    /* Plugin-owned UTF-8 JSON: {"ranges":[{"id":"...","startTick":0,"endTick":48}]}. */
    svs_status (SVS_CALL *query_ranges)(svs_session, const char** result_json);
    /* ABI 1.2 optional engine-wide declaration (no voice required).
       context_json carries current engineSettings and requested compute settings.
       Result: schemaVersion, name, engineType, engineSettings descriptors.
       Release the returned string using release_string; values reach submit
       through snapshot input_json, not mutable shared engine state. */
    svs_status (SVS_CALL *query_engine_settings)(svs_engine, const char* context_json, const char** result_json);
} svs_api;
/* Check the complete field before inspecting an optional pointer or host service. */
#define SVS_HAS_FIELD(value, type, field) ((value).size >= offsetof(type, field) + sizeof((value).field))
/* Required prefix excludes optional tail functions. */
#define SVS_API_REQUIRED_SIZE ((uint32_t)offsetof(svs_api, pronunciation))
typedef svs_status (SVS_CALL *svs_get_api_fn)(uint32_t, uint32_t, uint32_t, svs_api*);
SVS_EXPORT svs_status SVS_CALL svs_get_api(uint32_t major, uint32_t minor, uint32_t size, svs_api* api);
#ifdef __cplusplus
}
#endif
#endif
