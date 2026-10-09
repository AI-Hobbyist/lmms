# SVC M0–M5 implementation progress

Original plan: [LMMS SVC API支持计划书.md](../../LMMS%20SVC%20API支持计划书.md).

| Milestone | Implementation | Validation | Checkpoint |
| --- | --- | --- | --- |
| M0 | ABI 1.0, capability schema, bounded multipart parser, C consumer/reference engine, lifecycle and fixed algorithm contract | PASS: Release build; SVCContractTest + C-only SVCConsumer (2/2); no GUI changes | Commit/push checkpoint: feat(svc): add versioned SDK and streaming contracts |
| M1 | Silence-first segmentation, numeric setting models, contextual/padded effective ranges, OS-random SHA-256 paired WAV cache and partial manifests | PASS: Release SVCChunkCacheTest; threshold/silence/stereo/tail/limits; 16 concurrent unique cache pairs; failure/cancel manifests | Commit/push checkpoint: feat(svc): add frontend segmentation and paired cache |
| M2 | Independent SVC track/clip/views, immutable progressive playback, source-rate mapping, cache persistence, mixer routing and incomplete-export guard | PASS: SVCPlaybackTest (impulses, arbitrary chunk sizes, context, stale generations); M1 cache regression; SVCIntegrationTest (4 QtTest cases, native Windows, real mixer output, mute/solo, routing, clone/delete, save/restore, missing source/result, export rejection); native screenshot inspected, Chinese readable; window cleanup PASS | Commit/push checkpoint: feat(svc): add independent progressive audio tracks |
| M3 | Capability-driven knobs/enums and stable IDs, model/weight/speaker browser, independent connection settings and new-track defaults, A/B waveform and independent gains, theme colors/SVG, plugin Re-render button and track Re-render action | PASS: Release build; 5 SVC CTests including 6 native QtTest cases; A/B/overlay numbers, missing B silence, reference gain rerender, conditional/unavailable options, multi-weight/speaker persistence, defaults isolation; native Windows 100%/150% DPI screenshots, Chinese and SVG inspected, window cleanup PASS | Commit/push checkpoint: feat(svc): add capability-driven controls and A/B audition |
| M4 | Pending | Pending | Pending |
| M5 | Pending | Pending | Pending |

Local RVC integration target is `http://127.0.0.1:8000`, with no Bearer token required for testing. Production recommendation on final delivery: enable Bearer token authentication and configure credentials through the engine connection settings.

Initial tracked worktree was clean. Existing untracked build logs, reference trees and SDK artifacts are preserved. Milestones run sequentially; M0–M3 use the reference backend before M4 actual RVC integration. GUI validation uses native Windows Qt only; current layout work does not use Computer Use.

M0 checkpoint confirmed: `81138a7d5a4bd4ca3b6da3d74c95ca0615cf2017` on local master and origin/master.

M1 checkpoint confirmed: `167fc6c622e71abece4fbb09d8400e86fa74c276` on local master and origin/master.

M2 checkpoint confirmed: `b70875f75f70672220d490b0fab467814ea75045` on local master and origin/master.

User additions for M3: the SVC plugin window has a **Re-render button**; the SVC track context menu has a **Re-render menu item**. Changed conversion parameters are applied by re-rendering. Scope remains SVC support only.

M2 evidence: `build/tests/SVCIntegrationTest-results.txt` and the real-window screenshot `build/tests/svc/SVC-M2-native.png`. The SVC-specific guarded track-view dispatch fixes deletion before queued view creation. The corresponding legacy non-SVC raw-pointer queue is outside this task and remains follow-up work. Native routing tests use MixerView's normal create/move/delete entry points so its channel views stay synchronized with the model.

M3 evidence: `build/tests/svc/SVC-M3-controls-native.png`, `SVC-M3-audition-native.png` and their `-150.png` variants. Screenshots come from visible native Windows HWNDs after stable rendering; no offscreen rendering is used. SVC-only changes preserve existing plugin enum values. Reference conversion runs on one worker with a 32-task limit and at most two outstanding GUI notifications per task; decoded playback publication stays outside the audio callback. Comparison gains default to 0 dB, missing B is silent, overlay is not normalized, and audition is excluded from export. Parameter edits invalidate the conversion generation and retain previous B until explicit rerender.

SVC connection tokens are held in memory by default. Optional persistence uses Windows Credential Manager; projects and cache metadata contain no connection credentials. Global chunk defaults affect new SVC tracks only. Missing numeric bounds are explicitly marked as incomplete metadata rather than invented backend limits. Production deployment and plugin DLL deployment remain M5 work.
