# Continuous predicted pitch and re-prediction validation

Adjacent sung notes now share one displayed prediction curve. The display helper no longer splits solely because the note ID changes. DiffSinger feedback retains the full model-frame interval when it straddles contiguous sung notes; feedback still stops at a real rest or the final note. Prediction samples and PCM are unchanged. The host hashes the plugin binary in its audio-cache identity, so the updated feedback does not reuse an old plugin's cached audio metadata.

Two reproductions failed before the corresponding fixes: the helper produced three curves instead of two when adjacent samples had different note IDs; real Furina feedback still failed continuity across ticks 95 and 97 after removing that ID split because the preceding frame was clipped at the note boundary.

Validation on 2026-10-09:

- Native Windows Qt integration: `feedbackPitchInterpolation`, `nativePitchResetContextActions`, and `diffSingerExternalVoiceRendering` with `SVS_TEST_PITCH_CONTINUITY=1`: 5 passed, 0 failed, 0 skipped including setup/cleanup. Adjacent C4/E4 `la` notes connected, interpolation stayed bounded, and a genuine sample gap remained disconnected.
- Actual visible Windows editor screenshot: `validation/DiffSinger-continuous-pitch-native.png`. Inspected the continuous white prediction line and normally rendered Chinese singer name. The test closed its window after capture. No offscreen backend used.
- External Furina backend test: a fresh per-note prediction request generated **2 new SHA256 tensor files**; repeating that same request generated none. These nonce-keyed pitch tensors are saved only after the pitch-stage model run. Cached replay, re-prediction and uncached rendering produced identical PCM under the fixed seed. This confirms an actual new pitch request, even when the resulting sound remains the same.
- Runtime rebuilt in place: `build/Release/lmms.exe`, `build/Release/svs/SVSDiffSinger/SVSDiffSinger.dll`. Foreground PowerShell PTY, environment initialization, UTF-8 Tee logging and immediate exit checks used. `git diff --check` passed.

## Cache location verification

The cache root is the configured LMMS working directory followed by `cache/SVS`; DiffSinger uses its `DiffSinger` subdirectory for WAV audio and SHA256 tensor files. It is not automatically beside `lmms.exe`.

Read-only inspection of `D:/Program Files/LMMS/.lmmsrc.xml` found the installed copy still configured with `D:/UserData/Desktop/Project/lmms/build/Release/lmms-workspace/`. Its actual DiffSinger cache is therefore `D:/UserData/Desktop/Project/lmms/build/Release/lmms-workspace/cache/SVS/DiffSinger/`, which exists. The development copy uses the same working directory. No installed configuration was changed.

The independent backend test explicitly uses `D:/UserData/Desktop/Project/lmms/cache/SVS/DiffSinger/`. Its audition WAV remains `50e0b420aec3ba0972ca960ef75b8177374670e6b0fdd683c085a35dd7e60e1d.wav` in that directory.
