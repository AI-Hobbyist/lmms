# Native DiffSinger SVS engine

This engine uses the independent SVS C ABI and native ONNX Runtime CPU 1.23.0.
It has no LMMS/Qt, Python, .NET, TuneLab or OpenUtau runtime dependency.
Dependencies are frozen in `dependencies.lock.json`; voice packages are external
user resources and are never bundled.

At milestone A1 the engine discovers external packages recursively and exposes
resource images and voice capability languages. It rejects synthesis until
pronunciation/duration in A2 and full CPU inference in A3. No placeholder voice
or synthetic success audio is registered. Settings contain only rendering steps
(1–100, default 20) and multiple voicebank directories. Apply refreshes changed
directories; Rescan reloads applied roots. An empty catalog keeps the engine's
settings available. Install IDs are host-persisted and models/configuration/
dictionaries have content fingerprints independent of image bytes.

On Windows, run `buildtools/Get-DiffSingerOnnxSdk.ps1` through the project's
foreground logging pipeline to provision the SHA-256 checked official SDK in
the existing `build/_deps` tree. The script sets persistent `LMMS_ONNX_ROOT`;
dot-source `buildtools/Enter-LmmsEnvironment.ps1` before configure/build/test.
Enable `LMMS_BUILD_DIFFSINGER` in the existing development build. For an
independent SDK build enable `SVS_SDK_BUILD_DIFFSINGER` in the existing SDK build.
JSON/YAML sources are fetched at pinned commits and archive hashes. An explicitly
enabled engine with missing/wrong-version ORT fails configure clearly.

`SVSDiffSinger` writes its development package directly to
`build/Release/svs/SVSDiffSinger` beside the existing development installation.
`DiffSingerAbiTest <plugin DLL> [external voice root]` verifies ABI 1.0–1.3
negotiation, empty initial state, catalog refresh and resource lifetimes.
`DiffSingerCatalogTest <external voice root> <existing build directory>` checks
native discovery/metadata/identity fixtures and all six development voicebanks.
`DiffSingerModelProbe <fixture root> <matrix.json> <expected package count>`
reads configuration and creates CPU sessions one model at a time, recording
actual names/dtypes/ranks/static and symbolic dimensions. It does not execute
models or imply synthesis acceptance. A0's six-package matrix is in
`doc/svs/DiffSinger-six-package-matrix.json`.

The dictionary/language implementation must follow `tlds_ref`: select languages
from each voice's declared capability, retain each stage's token/language IDs,
and resolve entries/symbols/replacements in the reference priority order.
Missing automatic pronunciation support must produce a diagnostic.
