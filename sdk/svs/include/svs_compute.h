/* Independent SVS Compute ABI 1.0. No dependency on Qt, STL or ONNX Runtime. */
#ifndef SVS_COMPUTE_H
#define SVS_COMPUTE_H
#include <stdint.h>
#ifdef _WIN32
#define SVSC_CALL __cdecl
#ifdef SVSC_BUILD
#define SVSC_EXPORT __declspec(dllexport)
#else
#define SVSC_EXPORT
#endif
#else
#define SVSC_CALL
#define SVSC_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define SVSC_ABI_VERSION 0x00010000u
#define SVSC_FEATURE_CPU UINT64_C(1)
#define SVSC_FEATURE_DIRECTML UINT64_C(2)
#define SVSC_FEATURE_ISOLATED_WORKER UINT64_C(4)
#define SVSC_FEATURE_CANCEL UINT64_C(8)
#define SVSC_MAX_RANK 8u
#define SVSC_MAX_BUFFER_BYTES UINT64_C(536870912)

typedef uint64_t svsc_handle;
typedef int32_t svsc_status;
enum
{
	SVSC_OK = 0,
	SVSC_INVALID_ARGUMENT = 1,
	SVSC_UNAVAILABLE = 2,
	SVSC_BACKEND_FAILURE = 3,
	SVSC_CANCELLED = 4,
	SVSC_WORKER_LOST = 5,
	SVSC_VERSION_MISMATCH = 6,
	SVSC_LIMIT_EXCEEDED = 7,
	SVSC_INTERNAL_ERROR = 8
};

typedef uint32_t svsc_dtype;
enum
{
	SVSC_FLOAT32 = 1,
	SVSC_INT64 = 7,
	SVSC_BOOL = 9
};

typedef struct svsc_tensor
{
	uint32_t size;
	uint32_t dtype;
	const char* name;
	uint32_t rank;
	uint32_t reserved;
	const int64_t* dims;
	uint64_t byte_count;
	const void* data;
} svsc_tensor;

typedef struct svsc_model_desc
{
	uint32_t size;
	uint32_t seed;
	const char* authorized_root;
	const char* relative_path;
	const char* fingerprint;
} svsc_model_desc;

typedef struct svsc_session_desc
{
	uint32_t size;
	uint32_t allow_cpu_fallback;
	const char* backend;
	const char* device;
	const char* stage;
	const char* cpu_only_reason;
} svsc_session_desc;

typedef struct svsc_result
{
	uint32_t size;
	uint32_t tensor_count;
	const svsc_tensor* tensors;
	const char* execution_json;
} svsc_result;

typedef struct svsc_api
{
	uint32_t size;
	uint32_t abi_version;
	uint64_t features;
	const char* runtime_version;
	svsc_status(SVSC_CALL* create_context)(const char* runtime_directory, svsc_handle* context);
	svsc_status(SVSC_CALL* query_backend_devices)(svsc_handle context, char** json);
	svsc_status(SVSC_CALL* probe)(svsc_handle context, const char* backend, const char* device, char** json);
	svsc_status(SVSC_CALL* create_model)(svsc_handle context, const svsc_model_desc* desc, svsc_handle* model);
	svsc_status(SVSC_CALL* query_model_signature)(svsc_handle model, char** json);
	svsc_status(SVSC_CALL* create_session)(svsc_handle model, const svsc_session_desc* desc, svsc_handle* session);
	svsc_status(SVSC_CALL* create_run)(svsc_handle session, svsc_handle* run);
	svsc_status(SVSC_CALL* run)(svsc_handle run, const svsc_tensor* inputs, uint32_t count, svsc_handle* result);
	svsc_status(SVSC_CALL* cancel)(svsc_handle run);
	svsc_status(SVSC_CALL* get_result)(svsc_handle result, const svsc_result** view);
	void(SVSC_CALL* release_result)(svsc_handle result);
	void(SVSC_CALL* destroy_run)(svsc_handle run);
	void(SVSC_CALL* destroy_session)(svsc_handle session);
	void(SVSC_CALL* destroy_model)(svsc_handle model);
	void(SVSC_CALL* destroy_context)(svsc_handle context);
	void(SVSC_CALL* release_string)(char* string);
	const char*(SVSC_CALL* last_error)(void);
} svsc_api;

SVSC_EXPORT svsc_status SVSC_CALL svsc_get_api(uint32_t version, uint32_t size, const svsc_api** api);

#ifdef __cplusplus
}
#endif
#endif
