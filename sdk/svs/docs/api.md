# SVS C ABI and C++ conveniences

The public boundary is `include/svs.h`: C calling convention, fixed-width integers, UTF-8 strings, opaque handles, and extensible structures beginning with `uint32_t size`. No LMMS/Qt/STL object or C++ exception crosses that boundary. `svs.hpp`, `svs_curve.hpp`, and `svs_time.hpp` compile into the caller and use standard C++17; they do not add a C++ DLL ABI.

## Negotiation

Resolve `svs_get_api`, zero-initialize the caller's `svs_api`, and pass major 1, supported minor, and the caller's byte size. Require `SVS_OK`, matching major, `size >= SVS_API_REQUIRED_SIZE`, and every required prefix pointer. Minor 1 extends the minor 0 table after pronunciation. A minor 0 plugin remains usable; absent resources/ranges are unavailable. Check `SVS_HAS_FIELD(api, svs_api, field)` before inspecting each optional pointer. Feature bits describe interface families; voice editing capabilities still come from the queried Schema. A feature bit alone is insufficient to call a tail beyond returned size. Unknown extensions are ignored. Different pointer widths/architectures cannot share this ABI.

`svs_get_api` writes at most the supplied size. The required prefix ends before pronunciation and remains unchanged. Host service extensions follow the old log/progress prefix; plugins copy only the declared complete fields, and use local allocation when host buffer services are absent.

## Calls and ownership

| Family | Calls | Contract |
| --- | --- | --- |
| Engine | create_engine / destroy_engine / catalog / capabilities | Create with a host table whose context remains alive until all sessions and callbacks exit. Catalog/Schema text belongs to the plugin until release_string. |
| Pronunciation | pronunciation (optional) | UTF-8 JSON request/result. Result text is released through release_string, including a text pointer returned with an error. |
| Resource | open_resource / read_resource / close_resource (optional) | An ID from the catalog/manifest, never an arbitrary path. Descriptor has ID, MIME type, byte count and lowercase SHA-256. Its strings belong to the open resource. Read copies a bounded range into caller storage; close returns the handle to the plugin. Hosts must check limits, format and hash. |
| Session | create_session / submit / query_ranges / render / cancel / destroy_session | submit copies all input before returning. query_ranges returns plugin-owned JSON ranges after submit. render synchronously starts work on the host's worker, and returns an aggregate result for the submitted session. First example declares one whole-clip range. A plugin may internally refine that range and aggregate output without changing the required prefix. |
| Result | release_result | Result pointers remain valid until release_result. Copy audio and feedback while valid; release even on failure when a result was initialized. Return each pointer to its designated allocator. Do not resubmit or render again while retaining the previous result. |
| Host | log / progress / allocate_buffer / release_buffer / completed | Host owns returned buffers. Resource requests are at most 64 MiB; audio at most 128 MiB in LMMS, with 128 MiB total outstanding per instance. completed copies diagnostic text and queues delivery. Progress/log notifications may coalesce under load. No callback manipulates GUI directly. |

Call declaration functions serially per engine on the host data worker. A synthesis worker uses only an immutable snapshot. cancel is a thread-safe request and may race with render; a slot stays occupied until render actually exits. Native code must not throw through any function pointer. An in-process host cannot isolate a native crash.

Before shutdown: cancel tasks, join synthesis and declaration workers, discard obsolete queued deliveries, release results/resources, destroy sessions, destroy engines, then unload the library. A completion notification is informational: actual result publication still checks clip ID, generation, revision, and request ID. The callback does not authorize publishing a stale result.

## Inputs and outputs

Snapshot fields carry entity identity, generation/revision/request, voice ID, sample rate, borrowed note array, duration seconds and full immutable input JSON. Notes include stable ID, local real tick, duration tick, mapped local seconds, pitch in semitones, original lyric, language, reading, phoneme overrides and parameter JSON. Strings/arrays are borrowed only for submit; plugins must copy them.

Input JSON carries trackParameters, clipParameters, curves, capabilities, language, pronunciations, voice/project dictionaries, plugin/voice/dictionary versions, position, contentOffset, contentEndTick, secondsPerTick and tempoMap. `originSeconds` is the derived frozen mapping at local tick zero, useful to minimal engines consuming the already mapped note seconds. Engineering tick = position + local tick - contentOffset. A negative note-local or result-local time is permitted for lead phonemes. `svs_result.start_seconds` is the global start of returned PCM, including lead time; feedback segment seconds are local content seconds. Channels are interleaved float32; LMMS's initial stereo host accepts 8–192 kHz and finite sample data within its result bound. The host resamples prepared immutable PCM during playback.

`query_ranges` returns `{ "ranges": [{ "id": "clip", "startTick": 0, "endTick": 48 }] }`, expressed in continuous local content ticks. Range IDs are stable within the submitted snapshot; musical boundaries do not truncate continuous curves. PCM lead/tail placement remains explicit in result timing. The first example advertises synthesis.segmented=false and produces one aggregate clip result.

Feedback JSON contains pitch, phonemes and parameters. Pitch/phoneme segments identify noteId and startSeconds/durationSeconds; parameter feedback is read-only and declared separately. Failure error_json contains a stable numeric code, message and contextual clipId/requestId; engines may add noteId/parameterId/rangeId. Keep unknown diagnostic fields. LMMS adds track/clip/tick location to export diagnostics.

| Code | Name | Meaning |
| --- | --- | --- |
| 0 | SVS_OK | Call completed successfully |
| 1 | SVS_BAD_ABI | Unsupported major or insufficient table size |
| 2 | SVS_INVALID_INPUT | Invalid field/range/structure/voice |
| 3 | SVS_CANCELLED | Requested cancellation observed |
| 4 | SVS_FAILED | Initialization, allocation, query or synthesis failure |
| 5 | SVS_UNSUPPORTED | Optional operation or resource ID unavailable |

## Caller-side C++ wrapper

`svs_sdk::Engine` negotiates and owns an engine; `Session` owns its session; `Result`, `String`, and `Resource` release through the originating API. Shared private ownership keeps a session/engine alive while a returned result/string/resource survives the outer wrapper. The loaded DLL and host callback context must still outlive all wrappers. Exceptions are caller-local conveniences and never cross C ABI.

`Session::render()` is synchronous; call it on your data/synthesis worker, and join that worker before destroying its callback context or DLL. The wrapper does not create a hidden thread. Submit is rejected until an outstanding Result has been released. Resource::read enforces a byte limit and exact bounded progress; consumers separately validate MIME, hash, and image dimensions. LMMS's adapter performs the SHA-256 check.
