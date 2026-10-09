# Shared compute contract (ABI 1.0, B0 freeze)

`svs_compute.h` is a separate optional C ABI. Existing `svs.h` 1.0–1.3
tables and required prefixes remain unchanged. This header declares the
contract; B1 delivers its implementation. A header alone is not compute support.
All strings are UTF-8, all handles are opaque 64-bit identities; zero is invalid.
Calling convention is cdecl. Consumers negotiate major/minor and table size;
unknown major, larger required table, short descriptors and unknown enum values fail.
Features describe installed capabilities, not whether a particular GPU is usable.

## Ownership and bounds

Descriptor strings and input tensors are borrowed for the synchronous call only.
The library owns result tensors, dimensions, names and execution JSON until
`release_result`. No ORT/COM/STL/Qt objects cross this boundary. Strings returned
through `char**` require `release_string`; `last_error` is thread-local and borrowed
until the next call on that thread. Destroying a parent waits for or cancels its
children; no DLL unload while any context/model/session/run/result remains alive.
`create_run` allocates a cancellable identity before synchronous `run`, enabling
another thread to call `cancel`. A run is single-use. Cancellation never falls back.

Limits: rank 8; one tensor/message/shared buffer at most 512 MiB; aggregate in-flight
buffers per worker at most 1 GiB; at most 256 tensors/message, 1 MiB JSON/control,
32 cached sessions per worker and one active Run per worker. Reject negative
dimensions, overflow, byte mismatch, nonfinite float inputs/outputs and invalid bool
values. Scalars have rank zero and one element. Match all model input names/types/
static dimensions and repeated symbolic dimensions before execution.

Model paths must be relative, resolve within the canonical explicit authorized
root, and exist; external tensor data is validated against that same root,
including symlinks/junctions. No scripts or disk-wide model discovery. Fingerprint
includes model and external data. Seed rewrite preserves existing DiffSinger
random-node semantics and participates in model/session identity.

## Workers and error classes

One CPU worker plus at most one selected DirectML worker per context; a changed
device retires the previous GPU worker after its requests finish. Protocol 1 uses
request ID, worker epoch, model/session/run identities and validated tensor layout.
Large data uses bounded shared buffers owned by the client until acknowledgement;
worker output ownership transfers only after a complete validated response.
Disconnect, crash or timeout invalidates all handles of that worker epoch, releases
buffers, and returns WORKER_LOST. Discard late replies. Shutdown is bounded: signal
cancel, wait at most 5 seconds, terminate owned worker if needed and reap handles.
Session initialization timeout 120 seconds; run timeout 300 seconds.

CPU worker enables only CPU EP. DML worker disables memory patterns, uses
ORT_SEQUENTIAL and serializes Run. DML device and queue share a D3D12 device. Resolve
the saved DXGI LUID on every initialization, never use a saved enumeration index.
Software adapters are excluded. Availability requires D3D12 + DML + a successful
ORT Add session/run with actual DmlExecutionProvider profile nodes. Real models
have their own compatibility checks; successful Add is not proof for every model.

BACKEND_FAILURE and WORKER_LOST permit one whole-stage retry in the independent CPU
worker when allowed. Unsupported graph, EP initialization, device removal and
device OOM must be classified from backend evidence. INVALID_ARGUMENT (tensor,
model, path or data), cancellation and generic internal errors never retry.
CPU retry failure is final. An ORT partition with CPU nodes is distinct from
whole-stage fallback. Report node providers rather than claiming all-GPU execution.

## Host policy and engine capabilities

An AI engine opts in with `compute: {"protocolVersion":1,
"runtime":"svs-compute-1","supportedBackends":["cpu","directml"],
"stageConstraints":[{"stage":"vocoder","backend":"cpu","reason":"force_on_cpu"}]}`.
Absent/unknown declarations receive CPU; traditional engines receive CPU and are
not invalidated by GPU policy changes. No mandatory change to the legacy plugin ABI.

Freeze `computePolicy` before cache lookup and submit:
`requestedBackend`, `requestedDevice`, `effectiveBackend`, `effectiveDevice`,
`policyRevision`, `runtimeVersion`, `stageOverrides`, `fallbackReason`.
CPU device is `cpu`; DML device is `dxgi:<HighPart hex8>:<LowPart hex8>`.
Unavailable requested devices remain persisted; effective CPU reports a reason.
Apply increments revision and cancels/invalidates participating AI clips; Cancel
changes nothing. Preview, playback preparation, export and recovery use the same
snapshot. Audio callbacks never probe or create sessions.

Result `execution_json` records actual stage routes, requested/effective device,
fallback reason, worker epoch, runtime version, timing and node-provider evidence.
Cache identity uses effective route, CPU-only overrides, runtime/model/dictionary/
algorithm versions and seed. Revision is a stale-result gate, not an audio content
hash. Display switches do not affect cache. Mid-run fallback rekeys storage using
actual route; never store fallback PCM under a DML-success key. Missing-plugin
cache recovery keeps existing compatibility rules.

## Frozen numerical acceptance (before GPU comparison)

Same notes/lyrics/tempo/curves and seed, fresh stochastic sessions for CPU and GPU.
All stages require finite values, identical shapes and frame counts. Integer/bool
outputs are exact. Duration: atol 0.05 seconds, rtol 0.02, final layout within one
model frame and no negative durations. Linguistic embeddings: atol 0.002, rtol 0.02.
Pitch: RMS <= 0.25 semitones, maximum <= 1 semitone (voiced frames). Variance:
normalized RMS <= 0.03 and maximum <= 0.15 of the declared range. Acoustic mel:
RMS <= 0.15, maximum <= 1.0 in model log-mel units. Vocoder isolated on identical
mel/F0: PCM RMS error <= 0.02, maximum <= 0.15 (float [-1,1]). Full stochastic
chain: waveform correlation >= 0.80, normalized RMS error <= 0.50 and RMS level
difference <= 3 dB, identical start time and phoneme identities; layout <= one
frame and feedback checked with corresponding stage limits. Silent regions are
excluded only from correlation, not finite/shape checks. CPU-only stages remain
CPU in both chains. Failed tolerance is failure; do not loosen after results.
Profile must contain DML nodes for at least one actual acoustic chain in B3/B4.

Dependency freeze: Microsoft.ML.OnnxRuntime.DirectML 1.23.0 native x64 and its
declared Microsoft.AI.DirectML 1.15.4 package. Extract native files only; no managed
runtime. See `src/core/svs/compute/dependencies.lock.json` for hashes. Non-Windows
builds use native CPU ORT 1.23.0; DML queries return unavailable with a reason.
ORT MIT and DirectML redistributable license/notices accompany deployment.

Sources: [official DML EP options](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html),
[versioned provider API](https://github.com/microsoft/onnxruntime/blob/v1.23.0/include/onnxruntime/core/providers/dml/dml_provider_factory.h).
