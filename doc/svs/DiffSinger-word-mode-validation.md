# DiffSinger word_div / word_dur compatibility

Status: required native inference and regression validation PASS.

Scope: add the two word-mode linguistic inputs to the existing shared pitch /
variance caller. No SVS host, editor, ABI, segmentation, acoustic, or vocoder
changes. Existing unrelated Song/VST edits are excluded from this commit.

Reference: OpenUtau cloned into `refs/OpenUtau` from
https://github.com/openutau/OpenUtau.git at
`8945ee38832203caafccbd85ab4375783dc630f1`. CodeGraph was synced after cloning
and after editing. The local clone is reference material, not a submodule or
distributed part of the product.

Reference semantics are `DiffSingerUtils.PaddedWordDivAndDur` (lines 124–173),
`DsPitch.Process` (135–149), and `DsVariance.Process` (146–160). Boundaries
precede real vowel phones. Head/tail padding and inserted silence gaps are not
real phones; their frame counts still contribute to their groups. If there is
no vowel, the last real phone supplies the boundary. Divisions sum to the padded
token count and word durations sum to the original frame count.

Production selects word mode from actual ONNX input names, requiring both
`word_div` and `word_dur`; it retains `ph_dur` for phoneme-mode encoders. Word
classification uses the mapped symbols and dictionary of the consuming stage.
Both word tensors are int64 `[1, number_of_groups]`. Existing language inputs,
strict signature checks, cache input hashing, and predictor `ph_dur` stay intact.
No voice name/path special case or user voicebank edits were introduced.

Validation:

- `validation/DiffSinger-word-mode-native.txt`: exact grouping/frame assertions
  for padding, consonants, multiple vowels, inserted gaps, real AP, no vowels,
  zero-frame phones and mismatched input lengths. Real external duration and
  variance word-mode ONNX encoders run successfully; the actual variance model
  produces 81 finite breathiness frames. A deliberately constructed in-memory
  pitch-stage fixture uses that word-mode encoder through the production shared
  caller and completes pitch/acoustic/vocoder synthesis, finite non-silent PCM,
  feedback, cached replay and seeded uncached replay. This fixture is test-only;
  it is not a claim about the natural sound of a modified voicebank.
- `validation/DiffSinger-word-mode-six-voice.txt`: all six existing full-chain
  regressions PASS; PCM hashes remain unchanged, including parameter edits,
  cache replay, no-cache seeded replay and rest/tempo placement checks.
- `validation/DiffSinger-word-mode-external-voice.txt`: unmodified deepseek voice
  PASS; 103,654 stereo frames at 48 kHz, original audition SHA unchanged.
- Release engine rebuilt directly into
  `build/Release/svs/SVSDiffSinger/SVSDiffSinger.dll`, beside the existing
  `build/Release/lmms.exe` deployment.

The original optional repeat GUI validation was MANUAL/PENDING: native plugin
discovery crashed before entering DiffSinger, and the partial report
`validation/DiffSinger-word-mode-native-QtTest.txt` remains a historical non-PASS.
The subsequent authorized LADSPA diagnosis identified the Windows test host's
executable name as the cause. After fixing that test-only identity, the real
Windows external-voice editor test passed (see
`validation/LADSPA-release-and-DiffSinger-after.txt`, 4 passed / 0 failed).
The actual development Release application also started and closed normally.
See `LADSPA-windows-test-host-validation.md`. No offscreen mode or production
GUI workaround was used.
