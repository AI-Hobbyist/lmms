# Value formats

All text is UTF-8 JSON. IDs are stable machine values; names/translation keys are display metadata. Capability `schemaVersion` is 1. Start with the full and reduced example declarations (`full.json`, `minimal.json`); these files contain actual accepted values.

## Capabilities and parameters

Declare `languages`, `defaultLanguage`, `noteLanguage`, `parameters`, `feedbackParameters`, `pitch`, `phonemes`, `pronunciation`, and `synthesis` as applicable. Parameter IDs are unique across input and feedback declarations. Each parameter declares `id`, `name`, `scope` (`track`, `clip`, `note`, `phoneme`), `type`, and `default`. Numeric types (`float`, `int`) have finite min/max, positive step and an in-range default; integer bounds/step/default are integral. Logarithmic scales require a positive minimum. Boolean/string defaults use those JSON types. Enums store a stable choice `id`; their default names an existing choice. Optional group/order/unit/translationKey affect presentation. Optional `color` is a six-digit RGB string (`#RRGGBB`) supplied by the plugin for parameter curves and tabs; malformed values reject the declaration. If omitted, the host uses its theme highlight color. The shared parameter graph normalizes each visible curve using its own declared min/max and linear/log scale (bool uses 0/1, enum uses declared choice indices); the left axis belongs to the selected curve. No parameter-specific colors or numeric ranges are inferred by the host. `visibleWhen` compares stored parameter values; hiding does not delete data. Feedback parameters are read-only (`writable:false`).

`curve:true` enables an input lane. Float curves use `linear`, `hermite` or `step`; integer, boolean and enum curves use `step`. String parameters remain discrete fields. The full example's `example.label` returns note-ID-bound text in feedback `labels`; numeric/bool/enum inputs affect PCM and `example.energy` reports rendered energy. This extra feedback field is preserved as an extension, without introducing an extra editor lane.

Pitch input is `none`, `absolute` or `offset`. Continuous semitones use fractional MIDI pitch. Offset input requires `referencePitch`; the host derives offset from its retained absolute curve. Feedback availability is declared independently. Phoneme timing/attribute editing and minimum duration/maximum lead seconds are capability limits, not hardcoded UI constants. Synthesis declares float32/stereo, cancellation and concurrency. The initial example returns one aggregate range; internal range subdivision must preserve continuous curve evaluation.

## Dictionaries and pronunciation

```json
{"schemaVersion":1,"id":"example.en","version":"1","language":"en","phonemeSet":"example.multilingual.v1","entries":[{"text":"la","candidates":[{"reading":"la","phonemes":["l","a"]}]}]}
```

Validate versions, language, phoneme-set agreement, entries and each candidate symbol. Unknown symbols or malformed dictionaries produce diagnostics rather than silently changing input. The packaged zh/ja/en dictionaries are fixed test entries only. Chinese `重` demonstrates two candidates. Keep original lyric separate from chosen reading and generated analysis. Priority is manual phoneme override, manual reading, project dictionary, voice dictionary, default parser. The continuation marker comes from the pronunciation declaration. Unknown words remain editable and carry a fallback/diagnostic.

Notes carry overrides as `phonemes.segments`: each segment has `symbol`, note-relative `startTick`, positive `durationTicks` and optional `parameters`. Negative start is permitted within declared lead limits. Convert duration/lead limits through the frozen tempo map, including a tempo change within a note. Moving a note moves its relative overrides; restoring automatic analysis removes that override, not its lyric.

## Curves and time

Input curves are keyed by parameter ID (pitch uses `svs.pitch`). Points have fractional content-local `tick`, numeric `value`, optional discrete `valueId`, incoming/outgoing tangents, automatic-tangent flag and explicit discontinuity/segment interpolation. Gaps identify erased intervals. Evaluate only covered regions; use base values otherwise. Do not emit NaN as a gap marker. Use the shipped `svs_curve.hpp` evaluator for linear/step/constrained cubic Hermite; do not independently bucket by note, bar or render block.

Engineering tick = clip position + content-local tick - contentOffset. `tempoMap` is a sorted array of `{tick,secondsPerTick}`, beginning at tick zero with finite positive rates. `svs_time.hpp` integrates each interval and handles inverse mapping. `secondsPerTick` supplies constant-tempo fallback. `originSeconds` is the frozen mapping at content-local tick zero; the minimal C example uses this derived value to return the correct global PCM origin without implementing JSON tempo evaluation. A nonzero position/offset without an origin is rejected by that example. Snapshot note seconds are already mapped by the host.

PCM `start_seconds` is global time including lead; pitch/phoneme feedback segment seconds are content-local time. Split/copy must preserve interpolated boundary values and relative content mapping; never reuse source entity IDs for new copied entities. Result publication still requires matching clip/generation/revision/request, irrespective of a callback's arrival order.
