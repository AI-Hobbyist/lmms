/* SVS SDK v0.1. Public C ABI; no LMMS, Qt or C++ runtime types at this boundary. */
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
#define SVS_ABI_MINOR 0u
typedef void* svs_engine;
typedef void* svs_session;
typedef int32_t svs_status;
enum { SVS_OK=0, SVS_BAD_ABI=1, SVS_INVALID_INPUT=2, SVS_CANCELLED=3, SVS_FAILED=4, SVS_UNSUPPORTED=5 };
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
} svs_api;
/* Required prefix excludes optional tail functions. */
#define SVS_API_REQUIRED_SIZE ((uint32_t)offsetof(svs_api, pronunciation))
typedef svs_status (SVS_CALL *svs_get_api_fn)(uint32_t, uint32_t, uint32_t, svs_api*);
SVS_EXPORT svs_status SVS_CALL svs_get_api(uint32_t major, uint32_t minor, uint32_t size, svs_api* api);
#ifdef __cplusplus
}
#endif
#endif
