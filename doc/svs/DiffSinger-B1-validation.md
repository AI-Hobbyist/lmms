# B1 shared native compute runtime

Status: PASS (Windows x64, 2026-10-09). B2 host policy and B3 full DiffSinger
DirectML synthesis remain separate milestones.

## Implementation

`build/Release/plugins/SVSCompute.dll` implements the frozen standalone C ABI.
It has no Qt or ONNX Runtime dependency. `svs_compute.hpp` supplies dynamic loading,
RAII handles and parent/library lifetime retention. Results and strings are released
by the creating library. Closing parents invalidates descendant handles.

CPU and DirectML run in independent owned processes of
`build/Release/svs/compute/SVSComputeWorker.exe`. CPU sessions register only CPU EP.
The GPU DLLs are delay loaded: CPU inference remains usable when DirectML.dll is
absent. DML sessions resolve the requested DXGI LUID and use a matching D3D12
device/compute queue, sequential execution, disabled memory patterns and serialized
Run. There is no device-index-zero substitution.

IPC uses bounded framed JSON, named shared tensor buffers, protocol/request/epoch
checks and timeouts. Each context retains at most one CPU and one DML worker;
each worker caches at most 32 sessions and executes one RPC at a time. Shared
buffers/results have a 1 GiB context budget and 512 MiB tensor aggregate bounds.
Run cancellation sets a shared flag and terminates ORT RunOptions. Worker loss
invalidates its epoch; stale sessions fail rather than binding a new process.
CPU-only overrides and permitted backend failures use a distinct CPU worker;
invalid inputs and cancellation do not trigger inference retry.

DiffSinger's existing tensor adapter now consumes this interface for its CPU
stages. Its cache runtime version changed to avoid reusing pre-migration tensors.
The second AI plugin, `SVSComputeExample`, consumes the same interface through
the public SDK. It is a synthetic ONNX contract fixture, not a trained voicebank.
Its DLL, fixture and manifest are deployed in the existing Release svs directory.

## Evidence

| Requirement | Evidence |
| --- | --- |
| Native runtime and deployed second plugin build | `validation/B1-final-runtime-build.log` |
| float32/int64/bool, scalar/vector, byte/name/finite validation, result ownership, single-use Run, pre-cancel and active ORT cancellation | `validation/B1-conformance-final.log` |
| Both real LUIDs, real DML Add nodes, numerical equality, CPU-only and missing-device fallback | `validation/B1-conformance-final.log` |
| Terminated owned worker, stale session rejection, fresh epoch and successful restart | `validation/B1-conformance-final.log` |
| CPU inference without DirectML DLL, unavailable GPU diagnostics and restored runtime | `validation/B1-missing-directml-conformance.log`, `validation/B1-cpu-without-directml.log` |
| Second engine via SDK-only ABI 1.0 consumer: PCM, origin, ownership and cancellation | `validation/B1-second-engine-sdk.log` |
| Six real DiffSinger packages: CPU complete chain, replay, pitch-conditioned PCM, seed, model controls, manual phonemes, cancellation and bounded chunks | `validation/B1-diffsinger-cpu.log` |
| Existing SDK minimal ABI 1.0 and full ABI 1.3 examples | `validation/B1-old-minimal-abi.log`, `validation/B1-old-example-abi.log` |

GPU IDs: AMD Radeon 780M `dxgi:00000000:000148ca`; NVIDIA RTX 5060 Laptop
`dxgi:00000000:00016a3c`. Both profiles show DML nodes. These synthetic-node
checks establish B1 runtime behavior; they do not substitute for B3 acoustic
model/PCM comparison. No Qt GUI or offscreen validation was used in B1.

All commands ran in the conversation's foreground PowerShell PTY with the
environment script, Tee-Object and immediate exit-code checks. No source voice
assets were copied into the delivered runtime. Non-Windows paths are implemented
but not validated on this Windows machine; cross-platform validation is pending.
