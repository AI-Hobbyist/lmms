# VST3 implementation progress

Goal created 2026-10-04. Execute S0 through S8 in order and commit/push each completed stage.
Windows only: x64 DAW, x86/x64 plugin helpers. Optional dependencies may be obtained from
official online sources and included in the build as authorized by the user.

| Stage | Status | Evidence / remaining work |
| --- | --- | --- |
| S0 | PASS | Contract, call graph, frozen legacy project and entry inventory recorded. Windows x86/x64 protocol, proxy, MIDI, editor, native DLL project/command/snapshot/preview/export baseline PASS. Runtime Carla baseline unavailable with DummyCarla; gap recorded for S6. |
| S1 | NOT_RUN | Public process bridge and realtime queue. |
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
S0 is ready for its stage commit/push; subsequent stages remain unimplemented.
