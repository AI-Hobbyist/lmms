# SVC SDK 1.0

Public headers `svc.h` (C ABI) and `svc.hpp` (optional RAII) have no Qt/STL objects at the ABI boundary. The compiled protocol support library currently uses Qt 6 Core internally. Consumers link `SVCSDK::SDK`; engine implementations expose a `svc_engine` table negotiated with `SVC_ABI_VERSION`.

The `SVCConsumer` target is a C-only consumer of the reference engine. `SVCContractTest` covers fragmented multipart PCM events, bounded input, terminal validation and cancellation. Both build in the existing LMMS build tree under `sdk/svc`. The reference engine is a bounded PCM identity backend for contract testing, not a production converter. HTTP adapters consume audio-file bytes through the same input pull callback.

See [ABI and algorithm contract](docs/Contract.md) and [HTTP reference profile](docs/BackendProtocol.md).

## Installed SDK consumption

Install the SDK from the existing build tree into the existing development prefix:

```powershell
. ./buildtools/Enter-LmmsEnvironment.ps1
& cmake --install build/sdk/svc --config Release --prefix "$PWD/build/Release" 2>&1 | Tee-Object -FilePath build.log -Encoding utf8
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log; exit $buildExitCode }
```

External CMake projects use `find_package(SVCSDK 1 REQUIRED CONFIG)` and link `SVCSDK::SDK`; set `CMAKE_PREFIX_PATH` to the installation prefix and Qt 6. Public headers are installed under `include/svc`, the library under `lib`, and the C reference/consumer examples under `share/svc-sdk`. The reference engine is supplied as source and is not a required production dependency. Compile `Consumer.c` and `Reference.c` as C, link the installed support library and Qt Core, then run the consumer. No LMMS headers, objects, widgets or executable are required.

## Loadable engine modules

`svc_plugin.h` declares the optional `svc_plugin_entry_v1` symbol. Resolve it after loading the module; request `SVC_ABI_VERSION`, validate table size/version and required function pointers, then create a context with UTF-8 address and optional token. Context construction copies settings without network I/O. Run `engine->capabilities(context)` on the discovery worker; copy its bounded, context-owned JSON before releasing the context. Failed discovery is reported by `error(context)` and must not produce a usable online engine.

Keep the library and context alive through every job. Serialize `pump`/`cancel`; destroy all jobs, then the context, then unload the module. Callback bytes and error strings are borrowed, so copy them when retained. A job's terminal status, including closing boundary/EOF and successful cleanup, determines completion; receiving an audio or done callback alone does not. Host generation checks reject stale results after edits or cancellation.

LMMS deploys the RVC adapter to `build/Release/plugins/svcrvc.dll`. HTTP adapters upload raw WAV bytes with chunked transfer encoding, decode HTTP transfer chunks before multipart parsing, and retain bounded buffers. The local test service is `http://127.0.0.1:8000` with an empty token. Production services should require Bearer authentication; configure it in SVC settings, optionally using Windows Credential Manager. Tokens never enter project files or cache identities.

Model, weight, speaker, inference parameters and frontend splitting configuration belong to the SVC track. All clips on that track use those settings; each clip independently retains its audio, playback mapping and paired cache. The window's clip selector switches waveform/A/B audition. The window Re-render button and track Re-render menu item both queue the track's clips. Move clips to separate SVC tracks when distinct conversion settings are needed.
