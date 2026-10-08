# M3 — export acceptance complete

M2 commit `d4541d3983e6315f7fdcf3e94c2a05461a1638c7` was pushed to origin/master and independently checked with ls-remote before M3 started.

Initial implementation:

- `ProjectExport::capture` captures only direct Song SVS/Sample children. Muted tracks remain included; note/curve values and immutable SampleBuffer ownership are copied without requesting synthesis or reading voice audio caches.
- Captured SVS clips preserve position, content offset (`-startTimeOffset`), visible length, notes and curves. Audio clips retain their actual sample buffer, file reference, offset, reversal and amplification for later crop/WAV preparation.
- Existing TempoSnapshot owns captured global tempo automation. Meter automation which differs from the fixed current meter is rejected explicitly; no global timing redesign is attempted.
- Named losses identify host effects, private parameters/dictionaries, unknown clip-schema fields and missing/undecoded audio. The capture is not yet connected to the menu action.

Initial tracking (resolved by the final integration below):

1. Native capture compiled and its initial core validation passed (M3-snapshot-build.log, M3-snapshot-integration-build.log, M3-snapshot-QtTest.txt). The test checks identical native Song state, filename, modified flag and usable undo, includes missing voices/unrendered notes and muted tracks, excludes direct automation tracks, and proves captured notes/audio remain stable after later edits. Full export validation remains pending.
2. Unified Project mapping implemented and core-tested: clipped notes, stable overlapping ordering, sampled original pitch/gaps, global tempo and fixed meter, volume/pan/mute/solo. Overlapping pitch conflicts, private fields and quantization are named losses. Format-specific projection remains pending.
3. SampleClip-only audio preparation implemented and numerically tested. Complete files may be referenced; cropped/reversed buffers become stereo float PCM WAV companions. Multiple clips split into named entries. The tests verify exact first/last reversed samples, frame counts and offsets through tempo changes. No SVS render/cache is used. The bridge stages companions by basename and the JSON roundtrip proves audio references remain valid after task cleanup. Format-specific audio requirements remain pending.
4. Resolve format constraints and output options before serialization, including explicit single-track selection, named omissions, subtitle/graphic output semantics and sampling precision.
5. File-group preparation/transaction implemented and core-tested: same-volume staging, all-original backup, replacement and rollback, cancellation, nested companions, boundary validation and refusal to overwrite an unconfirmed newly appeared file. A real Windows exclusive file handle blocks the second original backup; the first original is restored and the whole group stays unchanged. Menu/controller integration, overwrite/loss dialogs and user-facing cancellation are still pending.
6. Run meaningful native/core transaction tests and real-window export interaction, then update the plan, review this task's diff, commit, push and verify M3 before starting M4.

The initial pending items above were completed by the final integration below. M4's 75 format-direction samples remain separate acceptance work.
Source review found why the M0 heuristic capability strings cannot be used as definitive export cardinality: LRC chooses only the first singing track; MusicXML emits every singing part; VSQX emits multiple singing tracks but only the first instrumental track, whose audio must be 44.1 kHz/16-bit WAV; Vogen emits multiple singing tracks but only the first tempo and meter. Actual source evidence must drive M3 limits and the M4 samples, rather than the provisional selection/splitting labels.

M3-core-QtTest.txt: 5 passes including initialization/cleanup, covering readonly capture, Project/audio mapping plus real embedded-runtime JSON export/import with companions, and complete-group transaction. M3-mapping-build.log, M3-companion-build.log and M3-transaction-build.log record foreground build results in the existing build tree. These are core/data tests; they do not claim M3 GUI acceptance or all-format serialization. No completed-stage commit was made.

## Final integration and acceptance

All initial implementation items are now connected to File → SVS 工程 → 导出SVS工程. Clicking captures the immutable snapshot before worker preparation or format selection. Export remains independent of voice availability and singing synthesis/cache. The themed save dialog exposes all 39 fixed export directions and aliases; the options dialog exposes actual output defaults/schema and explicit named track selection for restricted formats.

`export-policy.json` refines the provisional M0 heuristic with fixed-source cardinality and known restrictions. Original track indices are validated and remapped after projection; every omitted track is named. Lyrics, notation, graphics and synthesis-parameter outputs disclose their limited semantics. Known mix/title losses, shared accompaniment state, first-tempo storage, note-grid quantization and VSQ length limits are checked. Missing/incompatible accompaniment and WAV/rate/depth constraints fail before publication. Source review also confirmed that SVP and CCS do export accompaniment despite the initial heuristic; their actual branches are retained.

Inspection confirms host and format losses before conversion. Converter warnings are confirmed before publication. Unused companions are omitted. The complete returned group is checked, all existing targets are listed for overwrite approval, and the tested same-volume backup/rollback transaction publishes it. Companions without a project output are explicitly rejected.

| Final evidence | Result |
| --- | --- |
| M3-controller-core-regression.txt | 5 passes including init/cleanup: read-only snapshot, Project/audio mapping with real embedded JSON conversion and relocated companions, and complete file-group rollback |
| M3-export-policy-test.log | 6 embedded-runtime Python tests: all 39 singing cardinalities; original-index selection and named omissions; real WAV metadata and relative companions; UST legacy/default behavior; named field/time/length losses; no writes before approval; rejection of companion-only conversion |
| M3-export-native-parent.txt / M3-export-native-child.txt | Native Windows Qt flow passes file/options/loss/overwrite/process cancellation, unchanged previous output and Song state, successful export, Unicode converter readback, click-time snapshot after later edits, no voice/audio dependency and usable undo |
| M3-native-export-options.png | Real exposed Windows dialog captured after stable rendering and inspected: configured development theme, readable Chinese/Japanese/Korean, explicit named selection and usable output options |
| M3-bridge-regression.txt | 5 passes including catalog/task lifecycle, cancellation/timeout, unavailable runtime and worker crash |
| M3-export-controller-build.log / M3-export-native-build.log | Application and integration executable compiled successfully in the existing build tree |

The initial native run's final assertion assumed the target JSON used unified model field names. The assertion now performs actual fixed-converter readback. Final native testing uses `LMMS_DATA_DIR` and the development theme configured by `build/Release/.lmmsrc.xml`; the missing-theme capture was replaced and was not counted as acceptance.

Development executable: `D:/UserData/Desktop/Project/lmms/build/Release/lmms.exe`. SVS packages remain at `build/Release/svs/{SVSExample,SVSDiffSinger}`, normal plugins at `build/Release/plugins`, and runtime at `build/Release/svs-project`. No replacement build/deployment directory or Qt offscreen validation was used. The differently named integration harness does not verify all normal-plugin deployment.

M3 acceptance is complete; this record is part of its milestone commit. The push and remote SHA must be verified before M4 begins. All 75 real format-direction samples, bidirectional fidelity, aliases/multi-file outputs, complete deployed-app/plugin/runtime/license checks and usage documentation remain M4. The 39-policy test is preflight coverage, not 39 successful serializations or universal pitch fidelity. Unrelated Song/VST modifications are excluded from this checkpoint.
