# SVC SDK 1.0

Public headers `svc.h` (C ABI) and `svc.hpp` (optional RAII) have no Qt/STL objects at the ABI boundary. The compiled protocol support library currently uses Qt 6 Core internally. Consumers link `SVCSDK::SDK`; engine implementations expose a `svc_engine` table negotiated with `SVC_ABI_VERSION`.

The `SVCConsumer` target is a C-only consumer of the reference engine. `SVCContractTest` covers fragmented multipart PCM events, bounded input, terminal validation and cancellation. Both build in the existing LMMS build tree under `sdk/svc`. The reference engine is a bounded PCM identity backend for contract testing, not a production converter. HTTP adapters consume audio-file bytes through the same input pull callback.

See [ABI and algorithm contract](docs/Contract.md) and [HTTP reference profile](docs/BackendProtocol.md).
