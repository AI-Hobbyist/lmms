# Native SVS SDK 1.3

This SDK builds native Singing Voice Synthesis plugins independently of LMMS. The C ABI requires a matching platform and pointer width. C++17 conveniences are compiled into the caller. No Qt, LMMS internal header, TuneLab, .NET, Libjack, online service or model download is required by the examples.

## Build and install

Use a C11/C++17 compiler and CMake. With Visual Studio on Windows, the minimal C example enables MSVC's C11 atomics support. Run each command in the foreground; capture and check its exit code before continuing:

```powershell
& cmake -S sdk/svs -B build/svs-sdk -G 'Visual Studio 18 2026' -A x64 -DCMAKE_INSTALL_PREFIX="$PWD/build/svs-sdk-install" 2>&1 | Tee-Object -FilePath 'build.log' -Encoding utf8
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log -Tail 80; exit $buildExitCode }
```

Repeat that logging/exit-code pattern for `cmake --build build/svs-sdk --config Release`, then `cmake --install build/svs-sdk --config Release`. In a standalone SDK source archive, use `-S .` instead of `-S sdk/svs`. On other platforms select the corresponding generator/compiler.

The installation contains `include/svs`, `lib/cmake/SVSSDK`, documentation and example/tool sources under `share/svs-sdk`, plugin packages under `svs`, and `bin/SVSConformance`. Consumers use `find_package(SVSSDK CONFIG REQUIRED)` and `target_link_libraries(my_plugin PRIVATE SVSSDK::SDK)` with `CMAKE_PREFIX_PATH` pointing to this installation. The installed full and minimal example directories can each be configured and built independently. The installed tool sources also form a standalone CMake project.

Copy a plugin's entire package directory to LMMS's `svs` discovery root; do not copy only its library. `LMMS_SVS_PLUGIN_DIR` selects an additional root during development. Catalog IDs and manifest IDs must remain stable across upgrades.

## Examples and checks

`examples/minimal` is a pure C plugin exposing only the ABI 1.0 required prefix, one English voice and stereo PCM. The full C++ example (`plugins/SVSExample` in the LMMS tree, `examples/full` in a standalone archive) exposes full and reduced voices, three small language dictionaries, parameters, continuous/discrete curves, pronunciation/phoneme/pitch feedback, resources, progress, cancellation and controlled development faults. Its deterministic synthesis is a host/SDK fixture, not a production singing model or language coverage claim.

Run `SVSConformance <absolute plugin-library path>` through the same foreground logging pattern. Exit 0 means the catalog, tested Schema fields, resources, audible finite PCM, nonzero origin, cancellation and result ownership passed. Exit 1 reports the first failed contract; exit 2 means invalid command usage. See [validation.md](docs/validation.md) for the tool's limits and host diagnostics.

Read [api.md](docs/api.md) for ABI, ownership and threads, [formats.md](docs/formats.md) for value formats, and [packages.md](docs/packages.md) for packaging and compatibility. Example SVG artwork is generated for this distribution. Local singer fixtures and reference application files are excluded from the SDK.

ABI 1.2 adds the optional engine-wide settings query and C++ helpers, preserving the ABI 1.0 mandatory prefix. See [EngineSettings.md](docs/EngineSettings.md) for engine names/types, global option descriptors, AI backend/device placeholders and immutable synthesis settings. The full example provides an output-gain option; the minimal C example remains a legacy-prefix compatibility fixture.

## Native DiffSinger CPU engine

Enable `SVS_SDK_BUILD_DIFFSINGER=ON` and set `LMMS_ONNX_ROOT` to the official extracted ONNX Runtime CPU 1.23.0 SDK. The independent SDK build does not link LMMS or Qt. JSON, YAML and Mandarin pronunciation dependencies are frozen with SHA256 checks in the engine CMake file. The installation includes engine source under `share/svs-sdk/examples/diffsinger`; it can be built against the installed `SVSSDK::SDK` target. See [packages.md](docs/packages.md) for optional pitch range metadata.

Deploy the entire `svs/SVSDiffSinger` folder, including `SVSDiffSinger.dll`, matching `onnxruntime.dll`, `manifest.json`, `data` and `licenses`. No singer models, reference application files, .NET or Python runtime are distributed. The empty engine catalog is valid. Configure **Settings → SVS → DiffSinger → Voicebank directories**, apply and rescan to load authorized external voicebanks. DirectML remains a placeholder in this A-series implementation; actual inference uses CPU.

The native engine resolves supported dictionary entries/manual phonemes and Mandarin lyrics, predicts duration, pitch and enabled variance controls, then runs the acoustic model and vocoder. Other declared languages remain selectable; missing language G2P support produces a diagnostic rather than claiming full language synthesis. Resolved pronunciation is displayed above each note; the original lyric remains inside it. Available feedback reference curves are declared read-only by the selected voicebank.

The host freezes the cache directory as `<workingDir>/cache/SVS/DiffSinger`. WAV filenames are full SHA256 hashes of the complete WAV bytes; logical synthesis identities use separate `.svsmeta` indices. Intermediate `.tensor` names use full SHA256 identities covering model/package/configuration/runtime/seed and tensor inputs, with stored data checksums. Directory and display preference changes do not change sound identity. The default deterministic seed is 1; explicit existing snapshot seeds remain supported without adding a seed settings control. Managed host and tensor disk caches each have a 512 MiB limit; arbitrary audition WAVs are retained.

Requests have bounded memory and model signatures. Long requests split at sufficient natural silence or explicit rests; an oversized continuous phrase is rejected with a clear diagnostic. Cancellation is checked between model stages and around cache/resampling work. It does not promise to interrupt an already executing ONNX kernel instantly. Failed model/configuration/port checks report the stage and voice context. Validation evidence and six-bank CPU results are recorded in the LMMS `doc/svs` A0–A4 records.

## Rest segmentation and parameter presentation

The optional `synthesis.segmented` capability enables independent rendering at empty beats. The host sends ordinary immutable synthesis snapshots for each segment, preserves unchanged validated PCM during local edits, and reports `Rendering current/total` in Song Editor. Parameters can declare `color` for the editable curve and tab; matching feedback IDs can share the color. Existing ABI 1.0–1.3 functions remain unchanged. See [formats.md](docs/formats.md#optional-rest-segmentation) for the declaration, padding bounds, curve context, cache dependencies and complete-export contract. `SVSConformance` validates these optional declarations when present; legacy examples need neither field.
