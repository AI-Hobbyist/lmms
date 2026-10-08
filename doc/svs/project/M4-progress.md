# M4 — completed validation

M3 checkpoint `7a7c1ace3d993c2d815ead31e85384dfda420db9` was committed and pushed to origin/master; the remote SHA was verified before starting M4. Only this task's files will be staged; existing Song/VST and other unrelated work is preserved.

## Frozen acceptance

The scope remains 40 plugins, 36 import directions, 39 export directions and every registered suffix, at LibreSVIP `e33dc2824453a3104ee2b0a9cd836dce8452ce31`. Vendor sources and the M1-frozen embedded Python/wheels have not changed. M0 numeric limits remain 0.5 LibreSVIP tick / 0.5 cent, native mapping 1e-9 ticks / 1e-6 semitones and one audio sample. Output-kind and target-chain losses are separate from full edited-pitch fidelity PASS.

## Real formats and minimum integration adapters

`M4-direction-test.log`, `M4/direction-report.json` and generated native fixtures contain actual serialization, parser results, aliases, delivered file groups, Unicode text/paths, note/key/lyric/pronunciation, pitch samples and silent gaps, tempo/meter, track state and resource hashes. The VSHP import-only fixture is genuine deterministic VSPD binary from the frozen construct model, with two notes, a nonflat edited curve and a real silence gap. No JSON substitute or unavailable inverse conversion is used.

The final current-code direction sweep passed 39/39 real serializers and 36/36 real parsers, including all aliases. `M4-adapters-test.log` now reports 18 tests passed, zero failed, including:

- Actual ACET ZIP and compressed MusicXML MXL containers; MusicXML native tempo offsets and merged initial divisions/time attributes.
- VXF complete UTF-8 packets, unique VSQ control keys, and UST's existing UTF-8 option.
- Early/late tempo events; VSQX native absolute clocks and XVSQ Clock metadata; each fixed converter's pre-measure boundary.
- MUTA zero-based parts preserving first-note pitch, S5P one-tick edited controls and its simulator tempo basis, UFData absolute unvoiced markers.
- CCS shared pitch tempo basis and explicit frame zero for CCS/VoiSona/mobile; NN's relative bend scale; DS millisecond axis, converted segment track and first-note pitch.
- Real silence gaps for nine controller formats, including MIDI. MIDI's sensitivity is converted from semitones to cents and its zero wheel remains voiced inside notes.
- JSON/SVP/MusicXML later meter changes; SVP's meter pre-measure boundary, existing VSQX 3 output option and UST 2 multiple native TRACKEND sections.
- Preflight names pitch, tempo, meter, note time, pronunciation and mix losses; unaccepted loss prevents output writing.

Failing real-process reproductions were retained as focused logs. These are data-conversion boundary changes, not a host architecture expansion or changes to production GUI rendering.

The final current-code `M4-audio-test.log` passed all 23 accompaniment formats with actual write/read, exact selected track count, resource presence, source/extracted WAV SHA-256 equality and unchanged half-tick offsets. All 23 current-code cases passed. Minimum adapters preserve source-adjacent ACE/VSQX resources, correct DeepVocal audio prefixes, retain XVSQ BGM, extract PocketSinger native embedded audio and return actual VFP extracted paths. VSQX companions are in the required .wavparts group. Embedded extraction defaults on; disabling it yields an explicit loss.

`export-policy.json` and `M4-loss-review.md` document named readback field losses, fixed sampling units, single-tempo / first-meter / fixed-meter restrictions and note grids. Import results receive the same field warnings. `project-fidelity-report.py` checks real measured differences against losses disclosed before serialization. Every unexplained difference fails the audit. A declared loss stays a full-fidelity failure; neither a nonflat line nor successful parsing turns it into a pitch PASS. The final current-code audit passed with declared losses and zero unexplained failures (`M4-fidelity-test.log`, `M4-validation.md`, `M4/fidelity-validation.json`). Nineteen formats pass strict edited-pitch sampling; other results remain explicitly LOSS, not full pitch fidelity PASS. Identical consecutive meter events are semantically redundant; the native mapper accepts them. Raw events remain in the report, and actual meter changes retain their bar indices for comparison.

## Original deployment and native Windows evidence

`M4-deployed-build.log`: foreground Release build passed for lmms, SVSIntegrationTest, SVSExample and all 81 enabled plugin/support DLL targets. The 58 ordinary LMMS entry/support DLLs remain in build/Release/plugins, 22 existing LADSPA binaries remain in build/plugins/ladspa/Release, and SVSDiffSinger remains in its existing SVS package directory. No replacement build or deployment was created. Disabled sid.dll is retired in place; no loadable disabled sid/gigplayer DLL remains. Executable: D:/UserData/Desktop/Project/lmms/build/Release/lmms.exe.

`M4-native-QtTest.txt`: nine passed, zero failed/skipped on native Windows Qt 6.10.3. Menu/default voice/pitch/export windows, controller flow, transactional file-group rollback, mapping/audio, read-only snapshots and import persistence/rollback passed. Four M4 native screenshots were inspected; Chinese/Japanese/Korean glyphs render normally. Prior M2/M3 screenshots were restored after copying the new captures. The dedicated QtTest text is authoritative: another activity overwrote the shared build.log, so the copied M4-native-test.log contains an older function list and is not test-result evidence.

`M4-native-deployment-test.log`, `M4/native-deployment-report.json`, `M4/deployed-native-main.png`: the actual development executable initialized SVSExample, exposed a stable 1280x800 main window on the normal Windows backend, was captured from desktop pixels, and closed normally with exit zero. Startup splash is excluded by reselecting the stable window. Chinese text and configured theme were inspected. Only the test's own process was closed and the development configuration was restored. No offscreen mode or Computer Use was used.

`M4/deployment-report.json`: technical deployment audit checks all 81 DLLs, qwindows platform, deployed/source bridge and manifests, all 52 frozen wheel hashes and installed members, and 51 exact dependency versions. The final current-source deployment rerun passed (`M4-deployment-test.log`). The initial source-tree comparison was corrected to compare installed wheel members because source PO/QML and compiled wheel resources differ by packaging; no vendor file or frozen hash was changed.

Supplemental notices and exact wheel metadata for six distributions are in tools/svs-project/licenses and copied into the existing runtime. Git-blob and SHA-256 checks pin the captured notice bytes. loguru/protobuf-py-ext/that-depends use exact release refs; ko-pron lacks tags and uses its pinned source notice. Original wanakana-python MIT source and MPL-2.0 wheel declarations plus canonical MPL text are retained. jyutping supplies a metadata-only MIT declaration with no upstream LICENSE file. These two upstream notice reviews remain non-blocking MANUAL/PENDING; no grant was invented or replaced. Technical checks remain separate.

`usage.md` explains the native workflows, explicit loss confirmation, encoding, aliases and companion handling. All predefined M4 technical acceptance checks have passed. The two non-blocking upstream notice reviews remain MANUAL/PENDING. Milestone checkpoint: records updated before inspecting status/diff, committing only M4 and pushing master; the exact checkpoint SHA and remote verification are reported with the completed commit.
