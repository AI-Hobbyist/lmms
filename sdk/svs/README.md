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
