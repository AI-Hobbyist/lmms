# M2 — complete

M1 checkpoint `74d01e0f971225e65eb2581e819ed9b2fd86a8cf` was pushed and verified before M2 implementation. M2 meets its original milestone acceptance criteria; M3 starts only after the M2 commit/push checkpoint.

Delivered:

- Exact File → SVS 工程 menu with 导入SVS工程 / 导出SVS工程. Import uses the themed LMMS file dialog, runtime format catalog and stable default-voice selection. Export remains disabled until M3.
- Independent preflight XML mapper preserves fractional note time, multilingual lyrics/pronunciation, track gain/pan/mute/solo and absolute original pitch. First-measure offset, cent-to-semitone conversion, interruption boundaries and fresh entity identities are explicit. Integer tempo quantization is disclosed; unrepresentable changing meters fail before replacing Song.
- Every singing track binds to the selected engine/voice IDs. Original pitch is editable, persists through native save/reopen, reaches synthesis input and survives edit/undo. Import pitch/use-edited-pitch are enabled; SVP uses full pitch including parser-supported vibrato.
- Durable audio is copied/decoded before commit. Repeated file references share the durable copy while each offset receives the correct length through tempo changes. Missing audio and unsupported fields are named losses. Worker warnings and structured losses reach the confirmation dialog; absent usable parser pitch is explicitly unverified, never claimed fully faithful.
- Commit occurs only after preflight, loss approval, the existing unsaved-project guard and voice revalidation. Success is a dirty unnamed new native project. Failed native restore rolls back original tracks, curves, filename and modified flag. Cancellation or failure removes this task's audio directory.

Validation:

| Evidence | Result |
| --- | --- |
| M2-mapper-QtTest.txt | 5 passes: fractional/overlapping notes, IDs, multilingual text, curve sampling/gaps, XML persistence, quantization, audio offsets and structural rejection |
| M2-final-QtTest.txt | 5 passes: native Windows persistence/rollback, menu/dialog/piano roll, and full controller flow; includes initialization/cleanup |
| M2-no-voice-QtTest.txt | 3 passes including actual native empty-voice dialog and disabled acceptance |
| M2-bridge-regression.txt | 5 passes: all 40 runtime catalog entries, task lifecycle, cancel, timeout, missing runtime and worker crash |
| M2-final-build.log / M2-final-integration-build.log | Production application and final native integration executable built in the existing build tree |
| M2-native-menu.png / M2-native-voice-dialog.png / M2-native-imported-pitch.png | Real Windows HWND captures inspected: readable Chinese, stable themed layout, visible editable nonflat pitch and disconnected gaps |

The full controller test uses the real deployed conversion runtime and actual modal dialogs. It checks unsaved Cancel, voice cancellation, process cancellation, named pitch-loss cancellation, corrupt input, actual Save failure, successful replacement, two audio offsets (96/120 ticks), durable resources after save/reopen, and unchanged source bytes. Cancellation/failure leave the resource directory at its baseline. Native persistence additionally samples alternating vibrato anchors, tempo boundaries, synthesis input and journal undo.

Required minimal fix: native project clear/reopen exposed `SampleTrackView::corruptStateUpdate` dereferencing a cleared QPointer while its closed view awaited deferred deletion. A pre-import native reproduction and linker address identified that callback; keeping the default audio device did not remove the failure. The fix returns when the model is absent. Both the smaller native reproduction and full import/save/reopen pass. Temporary symbol-map linker configuration and debug probes were removed.

Development executable: `D:/UserData/Desktop/Project/lmms/build/Release/lmms.exe`. SVS packages remain at `build/Release/svs/{SVSExample,SVSDiffSinger}`; normal development plugins remain at `build/Release/plugins`. The integration harness's different executable name causes ordinary DLL host-import warnings; these do not constitute normal-plugin validation. Full deployed plugin validation remains M4. No offscreen path or replacement build/deployment directory was used.

All 75 real format-direction samples remain M4 acceptance work. M2 verifies the host mapping and transaction, not fidelity of every parser. No unrelated Song/VST edits are included in this milestone.