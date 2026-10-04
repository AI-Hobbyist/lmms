# VST3 implementation progress

Goal created 2026-10-04. Execute S0 through S8 in order and commit/push each completed stage.
Windows only: x64 DAW, x86/x64 plugin helpers. Optional dependencies may be obtained from
official online sources and included in the build as authorized by the user.

| Stage | Status | Evidence / remaining work |
| --- | --- | --- |
| S0 | PASS | Contract, call graph, frozen legacy project and entry inventory recorded. Windows x86/x64 protocol, proxy, MIDI, editor, native DLL project/command/snapshot/preview/export baseline PASS. Runtime Carla baseline unavailable with DummyCarla; gap recorded for S6. |
| S1 | PASS | Production VstPlugin/RemotePlugin migrated on Windows; dual-ABI supervised native helper, bounded control and preallocated realtime path. Full Release build and 41/41 CTest PASS (51.54 s). Stage delivered in the commit containing this completion record. |
| S2 | NOT_RUN | VST2 shell and compatibility migration. |
| S3 | NOT_RUN | VST3 adapter and dual-ABI SDK fixtures. |
| S4 | NOT_RUN | Catalog, scan roots and migration. |
| S5 | NOT_RUN | Unified entries, buses, latency and offline timeline. |
| S6 | NOT_RUN | Real Carla backend and per-node bridges. |
| S7 | NOT_RUN | WaveShell and automated fault matrix. |
| S8 | NOT_RUN | Release, dependency verification and ZIP. |

Initial existing CTest run: 13 PASS, 5 NOT_RUN because test executables had not been built.
Built the missing five tests and both existing helpers successfully. Latest Release CTest:
22/22 test executables PASS (2026-10-04, 13.64 s). Optional native-DLL integration inside
existing agent tests is still skipped unless its existing environment variable is supplied;
this executable-level result does not imply full VST entry coverage. The new dedicated
VstEntryPoints test separately uses actual Release vestige/vsteffect/vstbase DLLs and
records each helper PID/module for x86/x64 commands, presets, clone, undo/redo, effect
selection keys, saved/frozen project reopening, offline WAV export and keyboard preview.
It reports 6 PASS, 0 FAIL, 0 SKIP. File-picker/context/drop gestures are mapped statically
and tested at their common registry/loadFile seam; changed GUI routes need S5 coverage.
The new Vst2CompatibilityTest reports 6 PASS, 0 FAIL, 0 SKIP: initialization, x86/x64
legacy state/audio/MIDI/module isolation, x86/x64 native editor, cleanup.
Standalone VstHostProtocol passes on both x86 and x64 (0.02 s each).

Evidence remains under `build/vst-s0/`: `baseline-tests.log`, `proxy-results.txt`,
`final-build.log`, `entry-build.log`, `entry-results.txt`, `x86-tests.log`, `x64-tests.log`.
These generated logs are not committed.
The old program cache requires idleUpdate before currentProgram reflects effSetProgram;
the baseline drives that existing path. No production proxy behavior was changed in S0.
The old RelativePathsTest failed because a Visual Studio configuration subdirectory does
not resolve development data automatically; CTest now supplies LMMS_DATA_DIR explicitly.
Existing Release uses DummyCarla (carla-native-plugin and carla-standalone not found);
it cannot demonstrate real Carla isolation.

Manual listening, subjective UI, DPI/focus experience, manual activation and certification:
SKIPPED_MANUAL by user instruction; continue automated work. ARA shell excluded by plan.
S0 committed and pushed to origin/master as fa520bea1. S1 completion evidence follows
the chronological checkpoints below. S2-S8 remain unimplemented.

S1 work in progress (uncommitted): AudioQueue uses three preallocated slots with fixed
little-endian headers, aligned Windows Interlocked ownership words, bounded audio/event
payloads and no waiting in submit/receive. It rejects malformed dimensions, old sessions,
old generations and late block results. Queue tests: 3/3 PASS (5.77 s), including native
named mappings between processes and x64-to-x86. Logs: `build/vst-s0/audio-queue-build.log`
and `audio-queue-tests.log`. This queue is not yet connected to production VST processing;
the old synchronous proxy still has the hang risk described in the plan. No S1 completion
or stage commit is claimed until supervision and migration tests pass.

Windows supervisor tests: x64/x86 PASS (3.52 s), verifying suspended launch before Job
assignment, startup failure, live-child hang deadline, actual exception exit diagnostics,
Unicode/quoted arguments, descendant cleanup and 50 lifetimes with handle-count checks.
The independent watcher handles control deadlines; audio fault requests use an atomic
publication and are terminated on the watcher thread. Native exception fixtures suppress
Windows error-reporting UI so automated tests do not wait for a human response.
ControlChannel tests: 3/3 PASS (0.22 s), including x64-to-x86 process echo, bounded
mailbox backpressure, length/version rejection before allocation, timeout and disconnect.
Control/state and audio mappings are separate; neither writes native C++ object layouts.
Logs: `build/vst-s0/supervisor-{build,tests}.log` and
`build/vst-s0/control-channel-{build,tests}.log`.
These are S1 component results, not evidence that production VST2 hang recovery is complete.
Latest S1 component run (after audio-fault watcher support): 10/10 VstHost CTest entries
PASS, 0 failures, 3.88 s. Evidence: `build/vst-s0/s1-component-build.log` and
`s1-component-tests.log`. S1 remains IN_PROGRESS and uncommitted until HostSession,
VST2 migration and integrated plugin fault fixtures meet the phase criteria.

HostSession tests: 3/3 PASS (6.34 s), covering x64, x86 and x64-to-x86 helpers,
asynchronous process creation, state pause/resume barriers, generation replacement,
stale replies, hangs/crashes, audio deadline failure, and 50 resource lifetimes.
A process-exit race was fixed so the interval before the watchdog's next poll does
not misclassify a native exception as a normal disconnect.
Evidence: `build/vst-s0/host-session-{build,tests}.log`.

The existing RemoteVstPlugin32/64 now also accepts the HostSession mapping protocol.
Its VST2 lifecycle, state, program and native editor handlers are reused through a
control-only MessageTransport seam; audio uses the separate bounded shared queue.
Legacy control argument IDs retain their meanings in an explicitly encoded u32
length/ID envelope. Packet sizes/counts are validated before string allocation.
The frozen S0 DLL fixture is unchanged. Separate native fault DLLs inject crash/hang
at entry, audio, state, editor and close; each ABI exercises all ten combinations.
Latest native suite: 4/4 PASS (6.83 s). Tests cover the real DLL in the child only,
known gain samples, one-block startup silence, timestamped MIDI, parameters, programs,
chunk save/restore, editor ownership/show/hide/destruction, recovery generations and
continued exact output from a healthy peer through every fault.
Evidence: `build/vst-s0/session-helper-build.log`, `native-session-build.log`,
`native-session-tests.log`, `native-fault-build.log`, `native-fault-tests.log`.
These tests directly drive HostSession against the production helper; the existing
DAW VstPlugin/RemotePlugin facade still uses its old transport. S1 remains IN_PROGRESS
and uncommitted until that facade's realtime audio/MIDI/parameter path is migrated
and the full native entry regression passes. S2-S8 remain NOT_RUN.

Regression checkpoint after the transport seam: Release lmms.exe, vestige/vsteffect/
vstbase, both helpers and the dedicated native-entry executables rebuilt successfully.
19/19 selected CTest entries PASS (27.49 s): all VstHost components/native faults and
the original Vst2CompatibilityTest/VstEntryPoints. The healthy peer in the fault test
is checked after each failure/recovery; continuously scheduled 16-instance coverage
remains part of the later matrix. Evidence: `build/vst-s0/s1-native-regression-build.log`
and `s1-native-regression-tests.log`. S1 is still IN_PROGRESS, not committed or pushed;
the next required work is the DAW-side transport and realtime/offline facade migration.

## S1 completion checkpoint

Status: PASS. Windows Release full build succeeded, including all existing plugins,
RemoteZynAddSubFx, both RemoteVstPlugin helpers and all test executables. The first
attempt encountered LNK1114 access denied while replacing McpSecurityTest.lib; the
incremental retry succeeded. Evidence: `build/vst-s0/s1-full-release-build-attempt1.log`
and `s1-full-release-build.log`. Full CTest: 41/41 PASS, 0 failures, 51.54 s.
Evidence: `build/vst-s0/s1-full-release-tests.log`.

VstPlugin now installs LegacyHostBridge instead of the old synchronous FIFO on Windows.
Live processing, MIDI, parameters and tempo allocate zero on the audio caller thread;
audio has no IPC wait/control mutex. VST proxies no longer create unused legacy FIFO
mappings. Control barriers preserve native state/program/chunk behavior. Offline
requests return current-block samples with bounded helper deadlines. The original
Vst2CompatibilityTest and native-DLL VstEntryPoints both pass through the migrated
proxy, including project/preset/snapshot/preview/command/export paths.

Production bridge tests cover concurrent MIDI ingress, exact MIDI offsets, parameter
barriers, realtime/offline transition, actual native audio hang, silence, same-session
reopen with incremented generation, unloaded/reloaded gating, discarded old pending
parameters, known-wave recovery and 50 native resource lifetimes on both helper ABIs.
Native faults retain exception code 0xE0000042 through session diagnostics. Direct
native tests cover entry/audio/state/editor/close crash and hang with healthy-peer
output after each fault/recovery. Frozen S0 fixture remains unchanged.

Manual review stays SKIPPED_MANUAL by user instruction. S1 does not claim VST2 shell,
VST3, route latency compensation, full offline timeline/tails, additional buses,
real Carla or the continuously scheduled mixed 16-instance matrix; those remain in
S2-S7. Earlier IN_PROGRESS paragraphs are historical checkpoints, superseded here.
