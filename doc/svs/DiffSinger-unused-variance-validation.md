# DiffSinger unused variance stage rendering fix

Status: external voice synthesis and real-window host validation PASS.

The reported native error was reproduced before the fix with:

```powershell
DiffSingerSynthesisTest.exe --voice <external-voice-root> cache/SVS/DiffSinger <shared-vocoder-root>
```

The real deepseek voice uses an acoustic model with inputs `tokens`, `durations`,
`f0`, `gender`, `velocity`, `depth`, and `steps`. It consumes no energy,
breathiness, voicing, or tension inputs. Nevertheless, the old renderer executed
its physically present `dsvariance` stage. That stage's linguistic model expects
`tokens`, `word_div`, and `word_dur`, whereas the frame-based variance caller
provided `tokens` and `ph_dur`. This caused the exact input-count error in the
user's screenshot.

The fix consults the actual acoustic ONNX signature before invoking the optional
variance stage. When no variance parameter is consumed, the stage is neither
loaded nor executed. Required variance stages retain the existing full inference
path. No user model/configuration was edited, no missing input was fabricated,
and tensor signature validation remains strict. This does not claim support for
word-based variance inference when an acoustic model actually requires it.

The synthesis test now has an explicit single-external-voice mode, including
Windows Unicode arguments, actual finite/non-silent PCM, pitch feedback,
capability-matched variance feedback, cached replay, and seeded uncached replay.
The test failed before the fix with the reported variance/linguistic error and
passed after it: 103,654 stereo frames at 48 kHz, zero variance feedback curves
as declared by this voice.

- `validation/DiffSinger-deepseek-render-before.txt`: original failure.
- `validation/DiffSinger-deepseek-model-matrix.json`: actual ONNX signatures.
- `validation/DiffSinger-deepseek-render-after.txt`: successful audio and cache checks.
- `validation/DiffSinger-deepseek-native-QtTest.txt`: deployed DLL through the LMMS
  host and real native Windows Qt editor, 3 passed / 0 failed. Ready status,
  waveform, pitch feedback, and readable Chinese singer name were inspected.
- Local screenshot: `validation/DiffSinger-deepseek-native-ready.png`. It contains
  external voice artwork and is kept locally rather than distributed in Git.

Audition WAV: `cache/SVS/DiffSinger/fb5bfcb26eb6bc05e4f72c88d2eea4d9356093c7ab5ecc865af12f56352e2c90.wav`.
Its filename was verified against its file SHA-256. User voicebank/vocoder model
files and audition cache files are not included in the replacement package.

Separate follow-up observation: the native editor displays Velocity and
Expressiveness as zero while the engine descriptors declare defaults of one.
This is not investigated or changed in this rendering fix.

Six original voicebanks also passed full CPU synthesis regression, including
finite/non-silent PCM, all three required variance feedback curves, pitch/control
edits, manual phonemes, cached and uncached replay, seed changes, cancellation,
natural-rest chunks, tempo mapping and PCM bounds. Their audition hashes remain
unchanged. See `validation/DiffSinger-unused-variance-six-voice-synthesis.txt`.

The standalone SDK plugin was rebuilt and installed in the existing SDK
prefix. ABI 1.0–1.3 loading passed (`validation/DiffSinger-unused-variance-sdk-abi.txt`).
There are no ABI or production host/UI source changes in this fix.

The product fix was committed and pushed as
`20c1225c549cad5bcd7033e0a4db4a71b6a1abed`. The existing development executable
`build/Release/lmms.exe` was rebuilt with version `1.3.0-alpha.2.95+20c1225`.
The replacement package is `build/packages/lmms-enhanced-full-20c1225c5-win64.zip`,
66,040,330 bytes, SHA-256
`E9076EB274396879B7E461A53AD110F97F91F1A0CE5A9574709DD6FD6721FC7E`.
Package validation passed for all 3,485 runtime files plus four package entries,
360 PE architectures, official default tracks, private-data exclusions, and the
complete DiffSinger dependency folder. The shared vocoder folder ships its
README only. See `validation/DiffSinger-unused-variance-package.json`.
