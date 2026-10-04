# VST3 implementation progress

Goal created 2026-10-04. Execute S0 through S8 in order and commit/push each completed stage.
Windows only: x64 DAW, x86/x64 plugin helpers. Optional dependencies may be obtained from
official online sources and included in the build as authorized by the user.

| Stage | Status | Evidence / remaining work |
| --- | --- | --- |
| S0 | PASS | Contract, call graph, frozen legacy project and entry inventory recorded. Windows x86/x64 protocol, proxy, MIDI, editor, native DLL project/command/snapshot/preview/export baseline PASS. Runtime Carla baseline unavailable with DummyCarla; gap recorded for S6. |
| S1 | PASS | Production VstPlugin/RemotePlugin migrated on Windows; dual-ABI supervised native helper, bounded control and preallocated realtime path. Full Release build and 41/41 CTest PASS (51.54 s). Stage delivered in the commit containing this completion record. |
| S2 | PASS | Supervised shell full enumeration/selection, persistent unsigned shellid, production effect/instrument selection and GUI model feedback. Full Windows Release build PASS; 43/43 CTest PASS (52.14 s). Delivered in the commit containing this completion record. |
| S3 | PASS | Windows x64/Win32 native adapter and scanner integrated. Exact factory CIDs, Unicode bundles, lifecycle, float32/float64, state, ParamID, native editor, transport/events, feedback and MIDI/parameter/latency/I/O/reload restart paths verified. Full Windows Release build PASS; 49/49 CTest PASS (110.41 s), including 50 editor lifetimes per ABI. Delivered in the commit containing this completion record. |
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
the chronological checkpoints below. S0-S3 are complete; S4-S8 remain unfinished.
Earlier IN_PROGRESS checkpoints below are chronological history, superseded by the
latest completion record.

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

S1 committed and pushed to origin/master as 755b287ef; local HEAD and origin/master
matched 755b287ef3fdcf2357720cafde38a9a23d4d5423 after the push. S2 is IN_PROGRESS;
S3-S8 remain NOT_RUN. This next checkpoint will be included with the S2 stage commit.

## S2 native shell checkpoint (uncommitted)

RemoteVstPlugin accepts an optional unsigned decimal shell ID and publishes its raw
32-bit value through audioMasterCurrentId before invoking the module entry. The scan
command calls effOpen/category/effShellGetNextPlugin without editor/audio initialization.
It returns all IDs/names; empty/nonterminating/duplicate enumeration fails explicitly.
HostSession scan/create requests report Scan/Initialize fault stages. Separate shell
fixtures preserve the frozen S0 source and inject enumeration crash/hang/duplicates.

Vst2Shell and Vst2ShellCrossABI: 2/2 PASS, 1.84 s. Covers full enumeration, high-bit ID
0xf1020304 and ID 0x01000200, independent child PIDs and distinct known-gain audio,
invalid/unknown IDs, parent module exclusion, scanner cleanup and bounded scan failures.
The initial selected-ID test failed because the callback patch was misplaced; corrected
audioMasterCurrentId and restored audioMasterSizeWindow before this successful run.
Evidence: `build/vst-s0/s2-shell-build.log`, `s2-shell-tests.log`.

DAW proxy/instrument/effect adapters now carry shellid through constructor/key/XML/reload;
their build and added native compatibility tests are still pending at this checkpoint.
PE probing now validates DOS signature, minimum length and PE header bounds. S2 still
requires production scan/selection integration, shell project/preset/GUI coverage,
GUI parameter edit/automation feedback and the full ordinary VST2 regression before
its stage commit. No S2 completion or VST3 success is claimed.

S2 proxy checkpoint: Release lmms.exe, vstbase/vestige/vsteffect, both helpers and the
native proxy/entry/fault tests built successfully. Vst2CompatibilityTest now exercises
both shell IDs on x86/x64, persists unsigned shellid and chunk, recreates the selected
child and restores its parameter state, and rejects four malformed PE shapes in the
parent without loading native code. Selected regression: 10/10 CTest PASS, 36.25 s,
including shell, ordinary compatibility, native entry paths, realtime bridge/resource
recovery and both native fault suites. Evidence: `build/vst-s0/s2-proxy-build.log` and
`s2-proxy-tests.log`. Production scan/selection and GUI edit feedback remain pending;
S2 is still IN_PROGRESS, uncommitted. No build/test session remains running here.

## S2 completion — 2026-10-04

Status: **PASS / COMPLETE**. Windows x64 DAW and both x86/x64 native helpers built.
The final full Release CTest run passed **43/43**, 52.14 seconds. The VST2 compatibility
executable reports 11 PASS, 0 FAIL, 0 SKIP; actual DLL entry tests report 8 PASS,
0 FAIL, 0 SKIP. S0/S1 remain complete; S3–S8 still require their implementations.

Delivered behavior:
- Disposable supervised scanning performs entry/open/category/all-child enumeration
  and close in the helper, never loads the third-party module in the DAW, and fails
  on crashes, hangs, duplicate IDs or malformed responses. PE architecture probing
  validates signatures and bounds. Results explicitly distinguish shell/ordinary
  modules; shell IDs preserve all unsigned 32 bits.
- Selected child ID is available through audioMasterCurrentId before native entry.
  Independent Alpha/Beta fixtures prove distinct output for each ID on both ABIs.
  Unsupported IDs and root-shell audio initialization are rejected.
- Effect discovery returns every shell child, using the existing file key plus
  shellid. Vestige file loading offers a child selection; a single-child shell is
  selected by its actual ID. Ambiguous headless loads require a saved selection.
  XML, effect keys, preset restore, reload, clone, snapshot undo/redo and project
  reopen preserve the chosen identity. Ordinary VST2 keys and old state remain valid.
- Native begin/perform/end callbacks publish to the bounded preallocated queue.
  Control polling transfers batches to the proxy's Qt thread; bound instrument and
  effect models exist before opening parameter controls. A gesture creates one
  undo checkpoint, parameter values update without an echo to the same native
  instance, and undo/redo still propagate to it. Host setters suppress deliberate
  plugin echoes. Pending GUI tasks are scoped to the proxy lifetime.
- Both ABI editor fixtures perform actual native-window edits and verify the model,
  gesture signals, exact parameter value, undo/redo and echo suppression. Existing
  ordinary VST2 audio/MIDI/chunk/program/editor and native entry/fault/resource
  regressions pass. Instrument parameter-array cleanup was corrected while adding
  models at load time.

Evidence (generated under build/vst-s0, not committed): s2-scanner-build.log,
s2-scanner-tests.log, s2-gui-build-retry.log, s2-gui-helper-fix-build.log,
s2-gui-feedback-tests.log, s2-full-release-build-retry.log,
s2-shell-entry-build.log, s2-shell-entry-tests.log,
**s2-full-release-tests-final.log**, s2-compatibility-results.txt and
s2-entry-results.txt. Failed diagnostic attempts are retained separately: exported
thread-local state was moved into the implementation file; an obsolete undefined
kVstVersion preprocessor guard was removed from begin/end edit; the native shell
effect test now selects the actual catalog key rather than reconstructing it.

**Manual review SKIPPED as requested:** subjective listening, DPI/focus judgment,
commercial UI appearance and human licensing/activation review. These are not
reported as automated passes and do not delay later stages. S4 still owns cached,
cancellable multi-root discovery; S5 owns removal of control waits under model locks,
full routing and PDC; S6/S7 own real Carla/Waves and the mixed-instance fault matrix;
S8 owns final packaging. No VST3 or real Carla success is claimed by S2.

## S3 checkpoint: native factory, lifecycle, audio and state

S2 was committed and pushed as `7070ec7da143c51b233a71a0d106e426dfd677c3`.
S3 is still IN_PROGRESS; its changes are not a completed-stage commit.

- Standalone `tools/vsthost` references the local Steinberg SDK 3.8.0 without
  Qt or SDK examples/VSTGUI. The unchanged SDK Windows loader uses C++17
  because its u8string calls are incompatible with C++20; own host code uses C++20.
- `RemoteVstHost` uses the S1 HostSession protocol, private shared regions,
  generation checks, Job supervisor and bounded scan/close deadlines. Factory
  and module loading occur exclusively in the helper. A scan does not create
  components, controllers, audio activation or editors.
- Factory enumeration validates count before allocation, queries every index
  with Unicode/extended/basic fallback, rejects absent metadata/zero or duplicate
  CIDs, and sends all class categories with exact 16-byte identities. The DAW
  codec identifies audio classes and keeps controller classes distinct. Native
  bundle resolution uses the helper ABI and wide filesystem paths.
- Native instances validate the selected audio CID, initialize IComponent and
  IAudioProcessor, create a separate controller from its specified CID or reuse
  a same-object controller, connect available connection points, enumerate
  opaque ParamIDs and reverse successful lifecycle steps on close. Basic bus
  arrangements and channel counts are bounded and revalidated before allocation.
  All native planar buffers are prepared before activation. Stereo fixtures
  exercise float32 and float64-only negotiation and variable block sizes.
- Bounded IBStream state separates component and optional controller blobs;
  the controller receives initial/restored component state before its own state.
  Runtime tests restore changed gain and verify the resulting native audio.
  State, audio and controller objects never cross the wire as native structs.
- Vst3Catalog tests complete class metadata, Unicode bundle paths, every
  truncated catalog length, duplicate identity and eight native fault cases:
  invalid counts, missing metadata, duplicate CID, scan crash/hang, close
  crash/hang. Recovery scans pass after each fault; native exception codes and
  Scan/Shutdown stages survive supervision. No scanned module appears in parent.

Evidence under `build/vst-s3/` (generated, not committed):
`x64-catalog-tests-retry.log`, `x86-catalog-tests.log`,
`crossabi-catalog-tests.log`, `x64-state-build.log`, `x64-state-tests.log`,
`x86-state-build.log`, `x86-state-tests.log`, `crossabi-state-tests.log`.
Latest standalone CTest: 2/2 PASS on x64 (4.37 s) and 2/2 PASS on Win32 (4.38 s).
The x64-to-x86 processing/state runner also PASS. These are checkpoint results;
they do not imply completed MIDI/automation/editor, DAW entry integration, PDC,
commercial WaveShell coverage, real Carla or packaging.

Manual listening, subjective editor/DPI/focus and activation review remain
SKIPPED_MANUAL as requested; they do not hold this goal pending approval.

### S3 checkpoint update: parameters and native editor

The helper now installs an IComponentHandler with the existing bounded MPSC
queue. GUI callbacks enqueue begin/perform/end without allocation or waits;
host setters suppress synchronous echoes per instance and calling thread.
Opaque ParamIDs map to preallocated pending values, and bounded native
IParameterChanges/IParamValueQueue containers prepare processor edits without
per-block allocation. Output parameter points are collected for control replies;
invalid values/IDs or full queues fail explicitly. Restart flags are exposed in
the control reply; applying dynamic bus/latency changes remains later work.

State saving flushes pending parameters in a zero-sample processing block after
the audio barrier, so saved processor/controller values agree even before the
next audio callback. State restoration removes stale pending setters. Native
tests deliberately echo host setters and verify no duplicate GUI feedback.

The HWND editor is owned by the helper, exposes IPlugFrame resizing and runs on
the helper message-pump thread. It detaches/releases the native view before
controller termination and window destruction. Its automated fixture checks
the editor PID, begin/perform/end values, resulting audio, native-requested
317x173 resize, hide/reopen and removal of both windows at close. Embedded DAW
UI, focus/DPI experience, expanded editor fault fixtures and full MIDI/automation
sample offsets are not claimed by this checkpoint.

Latest verified sources: standalone x64 Release build and 2/2 CTest PASS (4.64 s),
Win32 Release build and 2/2 CTest PASS (5.07 s), plus x64-to-x86 editor/parameter/
state/audio runner PASS. Additional evidence: `x64-parameter-state-tests.log`,
`x64-editor-build-final.log`, `x64-editor-tests-final.log`,
`x86-editor-build.log`, `x86-editor-tests.log`, `crossabi-editor-tests.log`.
S3 remains IN_PROGRESS and has not been committed as a completed stage.

### S3 checkpoint update: sample-offset events

`Vst3BlockEvents` adds an SDK-free bounded event packet with explicit unsigned
ParamID, sample offset, event bus, MIDI channel and normalized value. Encoding
uses caller-owned storage; decoding validates the complete packet before queue
mutation. Native parameter points enter the bounded IParameterChanges queues;
note-on/off and poly-pressure enter a preallocated IEventList. Native MIDI output
is retained in a bounded control-readable buffer. Full buffers and invalid native
output fail explicitly. Close clears event bus and MIDI caches before reopening.

The SDK fixture now tests sample-offset gain changes plus MIDI gate/pressure,
and echoes MIDI output. Both channels match the exact expected transitions at
samples 7, 11, 17 and 23, for float32 and float64-only processors. Output preserves
bus/channel/pitch/value/offset. Every truncated nonempty input packet is rejected;
a bad later offset is rejected before invoking any consumer.

Latest evidence: `build/vst-s3/x64-events-build-final.log` and
`x64-events-tests-final.log` (2/2 PASS, 4.83 s),
`x86-events-build-final.log` and `x86-events-tests-final.log` (2/2 PASS, 5.10 s),
`crossabi-events-tests.log` (x64 parent to Win32 helper PASS).
S3 remains IN_PROGRESS: MIDI CC/pitch-bend/channel-pressure mapping and full
ProcessContext, controller synchronization after audio automation, output block
sequence attribution, additional native lifecycle faults and root build
integration still require implementation/verification. These checkpoint tests
do not prove those remaining requirements. No completed-stage commit is made yet.

### S3 checkpoint update: transport, MIDI mapping and output attribution

Audio input and output automation now retain the last sample-ordered parameter
point for subsequent control-thread controller synchronization. State saving
flushes pending processing changes and synchronizes the controller before
serializing both native states. Host setter echoes stay suppressed. Automated
state save/restore verifies matching processor/controller values after audio
automation and after MIDI mapping.

An optional version-2 input header carries playing/recording/cycle flags, tempo,
time signature, sample and musical positions, bar/cycle positions and continuous
sample time. The helper provides ProcessContext and a monotonic nanosecond system
time; positions advance after each successful block. MIDI events receive a
musical position at their sample offset. Complete wire validation precedes
transport/queue mutation. The native fixture verifies sample and continuous
positions across blocks, tempo 137.5, 3/8, bar/cycle values and event PPQ positions.
Optional SMPTE/chord/clock fields are not marked valid. Timeline routing and loop
wrapping in the DAW remain S5 work.

MIDI controller input uses controller numbers 0..127 for CC, 128 for channel
pressure and 129 for pitch bend. IMidiMapping is queried on the helper control
thread before processing and cached per input bus/channel/controller; audio
processing never calls that UI-thread interface. Assignments must identify a
known writable ParamID. Unmapped controllers are ignored. The fixture verifies
exact stereo transitions at offsets 5/13/21 for CC/pressure/bend, ignores an
unmapped channel, and saves the final normalized 0.875 in both native states.
Mapping refresh after restartComponent is still pending.

MIDI output now carries each original audio block sequence and length, including
valid sequence zero. A bounded SDK-free control codec preserves several blocks
until polling. Tests collect two unpolled 32/127-sample blocks and verify their
separate sequence, size and offset; every truncated reply and a malformed later
record fail before consumer mutation. DSP parameter feedback still needs the
equivalent block attribution. Nonfinite or float64 values exceeding float32 range
fail audio processing with silence rather than converting to infinity.

Latest verified sources: x64 Release build and 2/2 CTest PASS (4.78 s), Win32
Release build and 2/2 CTest PASS (5.50 s), x64 parent to Win32 helper processing
runner PASS. Evidence in `build/vst-s3/`: `x64-output-sequence-build-final.log`,
`x64-output-sequence-tests-final.log`, `x86-output-sequence-build-final.log`,
`x86-output-sequence-tests-final.log`, `crossabi-output-sequence-tests.log`.
An earlier overlapping build/test attempt had an invalid log and was discarded;
the final commands above ran sequentially. S3 remains IN_PROGRESS, uncommitted;
S0/S1/S2 remain completed and pushed. S4-S8 have not run. Manual review remains
SKIPPED_MANUAL by user instruction; no human-review dependency blocks progress.

### S3 checkpoint update: DSP feedback and MIDI assignment restart

Parameter polling now uses a bounded SDK-free version-2 codec. Native controller
begin/perform/end gestures use phases 0/1/2 with no audio block; processor output
uses phase 3 with its original block sequence, length and sample offset. Sequence
zero is valid. This prevents DSP output from being mistaken for a GUI gesture
and preserves attribution when several blocks are read together. Zero-sample
state flush output updates controller synchronization without fabricating an
audio event. Native tests verify a 64-sample output point, two unpolled 32/127
blocks and rejection of every truncated feedback envelope.

The helper services native control work between process calls. A
`kMidiCCAssignmentChanged` callback rebuilds the cached mapping and removes old
assignments before subsequent processing. Test MIDI learn moves CC 7 to CC 10:
the removed mapping leaves audio unchanged and the learned CC changes gain at
sample 7; the restart flag is also delivered to the parent. Mapping refresh
never calls the UI-thread SDK interface from `Vst3Instance::process`. Initial
latency is now read after successful component activation, matching SDK order.
Component reload, I/O/latency reactivation, parameter metadata/value refresh and
other restart branches remain unimplemented; this checkpoint does not claim
full restartComponent support or completed S3.

Latest verified sources: x64 Release build, 2/2 CTest PASS (5.09 s); Win32
Release build, 2/2 CTest PASS (5.35 s); x64 parent to Win32 helper PASS.
Evidence in `build/vst-s3/`: `x64-midi-restart-build.log`,
`x64-midi-restart-tests.log`, `x86-midi-restart-build.log`,
`x86-midi-restart-tests.log`,
`crossabi-parameter-feedback-midi-restart-tests.log`.
S3 remains IN_PROGRESS and uncommitted until its full requirements are verified;
S0-S2 are complete and pushed, S4-S8 remain NOT_RUN. Manual review stays
SKIPPED_MANUAL. The goal remains active.

### S3 checkpoint update: parameter refresh, latency and build integration

`kParamTitlesChanged` rereads bounded metadata and rebuilds parameter indices,
preserving pending values by unsigned ParamID across reordered parameters.
`kParamValuesChanged` invalidates old controller synchronization values and
queues current writable controller values for the processor. `kLatencyChanged`
stops processing, deactivates/reactivates the component, reads its updated latency
after activation and resumes processing. These operations execute between native
process calls on the helper owner thread. The native fixture enforces lifecycle
order and tests a simultaneous parameter reorder/title/default/step/value change
and latency update from 0 to 17. Audio then matches the new gain 0.3125.

Parameter command selector 1 returns an SDK-free bounded metadata snapshot:
channel totals, sample precision, current latency, exact ParamIDs, flags, steps,
unit IDs, current/default values and UTF-8 titles/short titles/units. Native UTF-16
titles are bounded and validated before conversion. Decoding rejects invalid
lengths/counts, duplicate IDs, nonfinite/range-invalid values and trailing bytes;
failure clears the destination. Tests check every truncated envelope and verify
that metadata/order changes do not alter parameter identity.

The main Windows MSVC build now creates independent `RemoteVstHost64` and
`RemoteVstHost32` ExternalProjects, copies their binaries into `plugins/` and
`plugins/32/`, and registers both for installation. The local SDK root remains a
CMake option; no SDK or Qt is linked into the DAW by this integration. Root
`LMMS_BUILD_VST_HOST_ONLY=ON` also configures/builds the native project without
entering the Qt/DAW dependency configuration. Both native Windows ABIs and their
fixtures are built from the root build. Generated binaries/SDK stay untracked.

`Vst3LifecycleTest` creates, resizes, hides/reopens, saves state, processes audio
and completely closes 50 fresh helper/editor instances. It alternates float32/
float64 CIDs and 48000/44100 Hz setups. Native windows disappear after close and
the parent never loads the module. Root x64 and cross-ABI runs report handles
131 -> 132 and 125 -> 126 respectively, within the explicit handle bound.

Latest evidence in `build/vst-s3/`: `root-integration-configure.log`,
`root-native-hosts-build.log`, `root-native-hosts-lifecycle-build.log`,
`root-native-tests.log` (6/6 PASS, 31.21 s), `root-host-only-configure.log`,
`root-host-only-build.log`, `root-host-only-tests.log` (3/3 PASS, 15.38 s).
Standalone x64 metadata/latency tests were 2/2 PASS (4.92 s), Win32 2/2 PASS
(4.97 s); root native tests also reverify the latest sources across ABIs.

S3 remains IN_PROGRESS and has no completed-stage commit. Component reload and
I/O rebuilding are still absent; additional lifecycle faults and the full DAW
Release build/regression remain to verify. No full restart support, completed
S3, completed S4-S8 or packaged release is claimed. Manual review remains
SKIPPED_MANUAL and the overall goal remains active.

### S3 completion: native Windows VST3 adapter

S3 is PASS. Component reload now saves opaque state, releases the native view,
controller, processor, factory and DLL, opens the same raw audio CID, restores
component/controller state and recreates the editor with its previous visibility.
The supervised helper PID remains unchanged. The fixture proves actual DLL unload
by resetting module globals, checks exact restored state and renders the expected
post-reload audio. I/O restart stops/deactivates the component, renegotiates buses,
reallocates native buffers/event maps and resumes; stereo -> mono -> reload stereo
is verified. These operations occur between native process calls, not in callbacks.

Final verification from the delivered sources:
- x64 standalone Release build PASS; 3/3 native CTest PASS (19.08 s).
- Win32 standalone Release build PASS; 3/3 native CTest PASS (16.91 s).
- x64 parent -> Win32 helper processing/reload/I/O runner PASS.
- Main Windows Release full build PASS; all 49/49 CTest PASS (110.41 s).
- Root native tests run installed-location helpers for both ABIs, including 50
  fresh editor/helper lifetimes each. Legacy VST2, shell, entry, state/export,
  supervision, protocol, audio queue and agent command regressions also PASS.

Evidence under build/vst-s3/: x64-reload-io-build.log,
x64-reload-io-tests-final.log, x86-reload-io-build.log,
x86-reload-io-tests.log, crossabi-reload-io-tests.log,
root-release-final-build.log, root-release-final-tests.log. The earlier overlapping
x64 test attempt had a log-writer conflict and is discarded. Final valid actions
ran sequentially and returned exit code zero. Build artifacts and the SDK remain
untracked. tools/vsthost/README.md documents the delivered adapter and its scope.

The native adapter is complete for S3; LMMS catalog/UI/project integration,
sidechain/multi-output routing and mixer latency compensation are S4/S5 work.
Real Carla is S6; commercial WaveShell and the broader initialization/audio/GUI/
state/unload fault matrix are S7. Optional unimplemented note-expression/MIDI2
interfaces are not advertised, and other optional restart flags are reported to
parent integration. Completion here does not claim those later stages passed.
Manual listening, subjective UI/DPI/focus checks, manual activation and
certification remain SKIPPED_MANUAL by user instruction. Continue S4; the overall
goal remains ACTIVE until S8 and its final status/artifact records are complete.
