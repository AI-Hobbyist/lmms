# DiffSinger phoneme aliases and external vocoder diagnosis

The native catalog recursively discovers `dsconfig.json`, `dsconfig.yaml`, or
`dsconfig.yml` containing `acoustic` or `fs2`; it does not impose a fixed nesting
depth. Predictor modules currently use the conventional `dsdur`, `dspitch`, and
`dsvariance` directories. Missing predictor configurations are optional. A
present configuration with missing referenced files remains an error.

The external deepseek fixture was rejected with `Invalid/duplicate phonemes ID`:
`zh/eng` and `zh/ueng` share token 191; `zh/io` and `zh/o` share token 205.
Phoneme aliases are now accepted without renumbering model tokens. Negative,
noninteger, or empty token declarations remain rejected, and the existing
language-ID uniqueness rule is unchanged.

After rebuilding the deployed native DLL, a read-only ABI 1.3 catalog query of
the same external package returns `Missing bundled/shared vocoder`. The package
contains no bundled `dsvocoder/vocoder.*`. Its acoustic configuration requests
`pc_nsf_hifigan_44.1k_hop512_128bin_2025.02`. No model or user voicebank file was
modified or copied into this repository.

Shared vocoders are resolved from each explicitly configured scan root:
`<root>/Vocoders/<configured-vocoder-name>/vocoder.{json,yaml,yml}`. The vocoder
configuration must reference its actual model and match the acoustic mel/audio
configuration. Parent directories outside the authorized roots are not searched.
For an OpenUtau `Singers` root, place the shared vocoder below `Singers/Vocoders`
and include `Singers` in the configured directory list.

Validation: native catalog regression checks phoneme aliases, an acoustic plus
vocoder package without any optional predictor, invalid token rejection,
required-vocoder rejection, and the existing six real voicebanks. The added
global shared vocoder setting is also validated in a real native Windows Qt
window with the development theme; no offscreen platform is used.

## Global shared vocoder setting

`diffsinger.vocoderDirectories` is an engine-wide directory list. The deployed
plugin defaults it to its sibling `svs/vocoders` directory, independent of the
process working directory. Multiple selected roots and multiple vocoder packages
are supported. Searches recurse within authorized roots, skip cycles and
out-of-root junctions, isolate unreadable subdirectories, and bound traversal.
An individual folder named after the configured vocoder may also be selected.
Explicit empty lists disable the default shared root.

Priority is bundled `dsvocoder/vocoder.*`, then explicit shared roots in configured
order, then legacy `<voicebank-root>/Vocoders` locations. Matching uses the
acoustic `vocoder` name and checks audio/mel compatibility. Invalid bundled
configurations never silently fall back. A shared directory edit invalidates the
native catalog query cache and uses the existing asynchronous host scan path.
Shared model updates invalidate synthesis fingerprints on rescan.

The default directory contains deployment instructions only. User models are
never copied into the replacement package. The shared-vocoder descriptor uses
existing SDK directory-list support and needs no ABI changes.

## Acceptance results

- PASS: native `DiffSingerCatalogTest refs/Singers build`, including the original
  six voicebanks and new alias/optional-module/shared-root/default-root/priority/
  shared-content-fingerprint cases (`validation/DiffSinger-global-vocoder-catalog.txt`).
- PASS: deployed native DLL ABI negotiation 1.0 through 1.3
  (`validation/DiffSinger-global-vocoder-abi.txt`).
- PASS: real Windows GUI with the development theme, default path plus added
  shared root, application and reopened settings; 3 passed, 0 failed
  (`validation/DiffSinger-global-vocoder-native-QtTest.txt`). The inspected real
  window screenshot is `validation/DiffSinger-global-vocoder-native-settings.png`.
- PASS: existing catalog/settings/resources plus asynchronous scanning regression;
  4 passed, 0 failed (`validation/DiffSinger-global-vocoder-regression-QtTest.txt`).
- CONFIRMED external limitation: read-only query of the reported deepseek package
  now reaches the required-vocoder diagnostic instead of rejecting valid aliases
  (`validation/DiffSinger-deepseek-catalog-diagnostic.txt`). Loading or rendering
  that package is pending installation of its requested matching vocoder.

No production source outside DiffSinger was changed. SDK API documentation and
example deployment were updated using the existing SDK build/install directories.
The replacement package adds only shared-folder instructions, never user vocoder
models. Historical captures overwritten by existing tests were restored.
