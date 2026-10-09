# SVS mute, solo and muted synthesis validation

Status: PASS (native Windows Qt, 2026-10-09).

The Song Editor SVS view omitted TrackView model binding: its mute and solo buttons changed their own fallback models instead of the track. Binding the view to SVSTrack restores the existing LMMS mute/solo behavior.

Muted SVS tracks cannot submit synthesis. Muting cancels pending/running clip jobs and rejects late results without deleting ready audio or segment caches. Unmuting schedules dirty clips; ready caches and an explicit user cancellation are retained. Solo-induced muting follows the same model notification.

Before the fix, `nativeSvsMuteAndSolo` failed because clicking mute left `isMuted()` false; `mutedSvsDefersSynthesis` failed because requests increased from 0 to 2 while muted. After the fix the targeted run reports 6 passed, 0 failed, 0 skipped: native buttons/solo restoration, left/right PCM energy, deferred edits, ready cache reuse, job cancellation/resumption, manual cancellation, cached playback/export and scheduler cancellation/budget.

Build: existing `build/Release/lmms.exe`; harness: `build/tests/Release/lmms.exe`; plugins loaded from `build/Release/plugins`, SVS engines from `build/Release/svs`. Commands run through foreground PowerShell PTY with the shared SDK environment and UTF-8 `build.log` pipeline. Native screenshot: `validation/SVS-mute-solo-native.png`. No offscreen backend used.

Pre-existing JACK server/Carla runtime warnings and export-name regular-expression warnings were observed; these do not fail the targeted assertions and are outside this fix.
