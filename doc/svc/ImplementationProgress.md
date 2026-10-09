# SVC M0–M5 implementation progress

Original plan: [LMMS SVC API支持计划书.md](../../LMMS%20SVC%20API支持计划书.md).

| Milestone | Implementation | Validation | Checkpoint |
| --- | --- | --- | --- |
| M0 | ABI 1.0, capability schema, bounded multipart parser, C consumer/reference engine, lifecycle and fixed algorithm contract | PASS: Release build; SVCContractTest + C-only SVCConsumer (2/2); no GUI changes | Commit/push checkpoint: feat(svc): add versioned SDK and streaming contracts |
| M1 | Silence-first segmentation, numeric setting models, contextual/padded effective ranges, OS-random SHA-256 paired WAV cache and partial manifests | PASS: Release SVCChunkCacheTest; threshold/silence/stereo/tail/limits; 16 concurrent unique cache pairs; failure/cancel manifests | Commit/push checkpoint: feat(svc): add frontend segmentation and paired cache |
| M2 | Independent SVC track/clip/views, immutable progressive playback, source-rate mapping, cache persistence, mixer routing and incomplete-export guard | PASS: SVCPlaybackTest (impulses, arbitrary chunk sizes, context, stale generations); M1 cache regression; SVCIntegrationTest (4 QtTest cases, native Windows, real mixer output, mute/solo, routing, clone/delete, save/restore, missing source/result, export rejection); native screenshot inspected, Chinese readable; window cleanup PASS | Commit/push checkpoint: feat(svc): add independent progressive audio tracks |
| M3 | Capability-driven knobs/enums and stable IDs, model/weight/speaker browser, independent connection settings and new-track defaults, A/B waveform and independent gains, theme colors/SVG, plugin Re-render button and track Re-render action | PASS: Release build; 5 SVC CTests including 6 native QtTest cases; A/B/overlay numbers, missing B silence, reference gain rerender, conditional/unavailable options, multi-weight/speaker persistence, defaults isolation; native Windows 100%/150% DPI screenshots, Chinese and SVG inspected, window cleanup PASS | Commit/push checkpoint: feat(svc): add capability-driven controls and A/B audition |
| M4 | RVC SDK module, asynchronous capability discovery, bounded raw HTTP streaming, weight-dependent controls and errors | PASS: Release build; 6 SVC CTests; controlled Chinese/multi-weight/single-multi-speaker/non-F0/index/error/cancel fixtures; live pm/rmvpe/fcpe/indexed/no-index (5 jobs), native progressive publication and cache restore; screenshot Chinese readable. Human voice A/B: MANUAL/PENDING | Commit/push checkpoint: feat(svc): integrate streaming RVC API backend |
| M5 | Installed SDK consumption and documentation, deployed plugins, transparent icon, asynchronous preparation/conversion/reconnection, bounded global retry round and incremental ZIP | PASS: installed independent C consumer; enabled delivery plugin build/deployment; 6 SVC CTests; live native disconnected-category hiding, two-attempt exhaustion, GUI-triggered recovery, responsive conversion, progressive publication and cache restore; 200% native window regression and cleanup; incremental archive SHA256 and installer VerifyOnly | Commit/push checkpoint: feat(svc): finish asynchronous workflow and incremental delivery |

Local RVC integration target is `http://127.0.0.1:8000`, with no Bearer token required for testing. Production recommendation on final delivery: enable Bearer token authentication and configure credentials through the engine connection settings.

Initial tracked worktree was clean. Existing untracked build logs, reference trees and SDK artifacts are preserved. Milestones run sequentially; M0–M3 use the reference backend before M4 actual RVC integration. GUI validation uses native Windows Qt only; current layout work does not use Computer Use.

M0 checkpoint confirmed: `81138a7d5a4bd4ca3b6da3d74c95ca0615cf2017` on local master and origin/master.

M1 checkpoint confirmed: `167fc6c622e71abece4fbb09d8400e86fa74c276` on local master and origin/master.

M2 checkpoint confirmed: `b70875f75f70672220d490b0fab467814ea75045` on local master and origin/master.

M3 checkpoint confirmed: `90efe192c2032577cf06827ecba7cde2b2c155e6` on local master and origin/master.

User additions for M3: the SVC plugin window has a **Re-render button**; the SVC track context menu has a **Re-render menu item**. Changed conversion parameters are applied by re-rendering. Scope remains SVC support only.

M2 evidence: `build/tests/SVCIntegrationTest-results.txt` and the real-window screenshot `build/tests/svc/SVC-M2-native.png`. The SVC-specific guarded track-view dispatch fixes deletion before queued view creation. The corresponding legacy non-SVC raw-pointer queue is outside this task and remains follow-up work. Native routing tests use MixerView's normal create/move/delete entry points so its channel views stay synchronized with the model.

M3 evidence: `build/tests/svc/SVC-M3-controls-native.png`, `SVC-M3-audition-native.png` and their `-150.png` variants. Screenshots come from visible native Windows HWNDs after stable rendering; no offscreen rendering is used. SVC-only changes preserve existing plugin enum values. Reference conversion runs on one worker with a 32-task limit and at most two outstanding GUI notifications per task; decoded playback publication stays outside the audio callback. Comparison gains default to 0 dB, missing B is silent, overlay is not normalized, and audition is excluded from export. Parameter edits invalidate the conversion generation and retain previous B until explicit rerender.

SVC connection tokens are held in memory by default. Optional persistence uses Windows Credential Manager; projects and cache metadata contain no connection credentials. Global chunk defaults affect new SVC tracks only. Missing numeric bounds are explicitly marked as incomplete metadata rather than invented backend limits. Production deployment and plugin DLL deployment remain M5 work.

M4 evidence: build/tests/svc/SVC-M4-RVC-native.png. Live API uses no Authorization header. The bounded fixture suite covers 401/413/422/429/503/408, truncated streams, request identity mismatch, cancellation and unsuccessful cleanup. Missing pitch_shift bounds use a -24..+24 initial knob range with editable numeric input; API bounds take precedence. Model and inference parameters are track-wide, while each clip keeps independent source, playback and cache. Both rerender entries render all clips on the track; clip selection switches waveform/A/B audition. A prior native test crash did not reproduce in isolated, full-suite and current native runs; the previously recorded legacy non-SVC queued-view issue remains outside scope.

User override: do not query other task status before builds/tests/packages (memory cost). M5 deliverable includes an incremental replacement ZIP in the existing packaging directory.

M4 checkpoint confirmed: 441c082f10217a6e8e850604875a1d48c3ab69b0 on local master and origin/master.

M5 evidence: `build/tests/svc/M5-regression.log`, `M5-live-reconnect.log`, `M5-native-200.log`, `M5-package-candidate-verify.log`, `SVC-M5-settings-native.png` and `SVC-M5-native-200.png`. Settings and track screenshots were captured from visible native Windows windows and inspected. Conversion moves silence analysis/segmentation off the GUI thread, then posts clip metadata and progress asynchronously. Queue limit remains 32 and pending GUI notification limit remains two; cancellation invalidates queued/prepared generations and shutdown wakes the worker before joining.

Global reconnect interval defaults to 5 seconds and maximum retries to 3. The start button launches one bounded round for disconnected engines, skips healthy engines and stops after exhaustion. Failed engines disappear from the browser but remain configurable in settings; successful discovery restores their categories. Saving unchanged connection settings preserves healthy connections. Live validation used interval 1 second and two attempts, checked that retries stop, restored the real API connection, and confirmed GUI timer activity during discovery and conversion.

Delivery executable: `build/Release/lmms.exe`; RVC module: `build/Release/plugins/svcrvc.dll`. All 52 enabled ordinary UI plugin targets and existing delivery imports were rebuilt in their configured directories before M5 validation. Installed external SDK consumer: `build/sdk/svc/Release/InstalledConsumer.exe`. Predefined retired SVS example DLL/manifest paths regenerated by installation were disabled in place with suffix `.disabled.20261009201645`; no SVS source changes were made. Final incremental archive and SHA256/manifest sidecars are generated under `build/packages` from the M5 source commit, against the existing full baseline `236c5f3fee98e677e375cb1aa1a7db431b904f2c`.

All automated milestone gates passed. Human voice A/B listening remains the plan-permitted non-blocking `MANUAL/PENDING`; synthetic live audio does not prove subjective voice quality.

## Post-delivery UI requests

Stage 1: RVC assumes a single model voice (speaker ID 0), independent of reported checkpoint speaker counts. Numeric API parameters declare slider presentation with adjacent editable values; enum parameters retain dropdowns. Missing pitch bounds initially use -24..+24 and numeric entry extends the slider range. SVC sidebar title is SVC, engine groups are top-level, and model/speaker entries reuse svc_track.svg. Default track names follow the selected model name while custom names remain unchanged. PASS: Release executable/module build; SVCRVCTest, SVCIntegrationTest and SVCPlaybackTest; actual native RVC slider/input synchronization, invalid resample exclusion, model naming, hidden speaker selector, responsive progressive conversion and cache restore. Evidence: build/tests/svc/RVC-sliders-regression.log, RVC-sliders-live.log and SVC-M4-RVC-native.png.

Stage 1 checkpoint confirmed: 0f03a9af6bdf720ccbe252c3eeec5c54b710ca3b on local master and origin/master.

Stage 2 complete: SVS voices moved to an independent sidebar without a root group; existing avatar/icon loader and voice drag/drop widgets are reused. The old instrument-browser SVS category and empty-SVS-track song-editor button are removed. Both SVC/SVS pages have instrument-style search, including engine names and case-insensitive leaf matching. Sidebar icons use the same orientation convention as existing sidebar pages.

PASS: Release lmms, SVCIntegrationTest and SVSIntegrationTest build; SVCIntegrationTest (7 passed, no failures, live case skipped because Stage 1 already validated it) and SVCRVCTest fixtures. Native Windows screenshots SVC-sidebar-main-native.png, SVS-sidebar-main-native.png and SVS-sidebar-search-native.png were inspected for icon direction, rootless groups, search controls and readable Chinese. Evidence: build/tests/svc/sidebar-direction-regression.log. Existing six-private-voice DiffSinger fixture regression: MANUAL/PENDING (SVS_DIFFSINGER_FIXTURE_ROOT unavailable); deterministic native SVS sidebar/search regression passed. Regenerated retired SVSExample DLL is disabled in place before delivery. The incremental package uses the existing full baseline 236c5f3fee98e677e375cb1aa1a7db431b904f2c and the Stage 2 source commit.
