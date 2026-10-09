#ifndef LMMS_SVC_H
#define LMMS_SVC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_ABI_VERSION 0x00010000u
#define SVC_MAX_HEADER 16384u
#define SVC_MAX_PART (8u * 1024u * 1024u)
#define SVC_MAX_FEED 65536u

typedef enum svc_status
{
	SVC_OK = 0,
	SVC_INVALID = 1,
	SVC_PROTOCOL = 2,
	SVC_CANCELLED = 3,
	SVC_FAILED = 4,
	SVC_COMPLETE = 5
} svc_status;

typedef enum svc_event_type
{
	SVC_START,
	SVC_AUDIO,
	SVC_PROGRESS,
	SVC_DONE,
	SVC_ERROR
} svc_event_type;

/* All pointers are borrowed until the callback returns. Copy before retaining.
 * Callbacks run on the calling worker thread, never on the audio thread.
 * Callback returns nonzero to cancel. No reentrant feed/destroy is allowed. */
typedef struct svc_event
{
	uint32_t size;
	svc_event_type type;
	uint64_t generation_id;
	uint64_t segment_id;
	const char* request_id;
	uint64_t chunk_index;
	uint64_t total_chunks;
	uint64_t sample_offset;
	uint64_t sample_count;
	uint32_t sample_rate;
	uint32_t channels;
	const uint8_t* bytes;
	size_t byte_count;
} svc_event;

typedef int (*svc_event_callback)(void* user, const svc_event* event);
/* Bounded pull: fill at most capacity bytes; 0 means EOF, negative means error.
 * Provider retains ownership. Engine must not read after cancel/destroy. */
typedef int64_t (*svc_input_read)(void* user, uint8_t* destination, size_t capacity);

typedef struct svc_request
{
	uint32_t size;
	uint32_t abi_version;
	uint64_t generation_id;
	uint64_t segment_id;
	const char* selection_json;
	svc_input_read read;
	void* input_user;
	svc_event_callback callback;
	void* callback_user;
} svc_request;

typedef struct svc_engine
{
	uint32_t size;
	uint32_t abi_version;
	const char* engine_id;
	/* Borrowed UTF-8 capability JSON until next engine call. */
	const char* (*capabilities)(void* engine);
	/* Copies request/selection; start emits no callbacks. Null means invalid. */
	void* (*start)(void* engine, const svc_request* request);
	/* Bounded work per pump. Host owns scheduling and serializes pump/cancel.
	 * cancel is idempotent; destroy joins/releases before returning. */
	svc_status (*pump)(void* job);
	void (*cancel)(void* job);
	void (*destroy)(void* job);
} svc_engine;

typedef struct svc_stream svc_stream;
typedef struct svc_stream_config
{
	uint32_t size;
	uint32_t abi_version;
	uint64_t generation_id;
	uint64_t segment_id;
	const char* boundary;
	svc_event_callback callback;
	void* user;
	/* Profile-specific completion requirements stay out of the host. */
	uint32_t require_cleanup;
} svc_stream_config;

svc_stream* svc_stream_create(const svc_stream_config* config);
svc_status svc_stream_feed(svc_stream* stream, const uint8_t* bytes, size_t count);
svc_status svc_stream_finish(svc_stream* stream);
void svc_stream_cancel(svc_stream* stream);
void svc_stream_destroy(svc_stream* stream);
const char* svc_stream_error(const svc_stream* stream);

/* Validates version, stable IDs, parameter metadata and discovery limits.
 * Error is copied into caller-owned buffer; no allocator crosses the ABI. */
svc_status svc_validate_capabilities(const char* json, size_t count, char* error, size_t capacity);
const svc_engine* svc_reference_engine(uint32_t requested_version);

#ifdef __cplusplus
}
#endif
#endif
