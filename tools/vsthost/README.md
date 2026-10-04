# Windows native VST3 host

`RemoteVstHost` is a Qt-free, per-instance Windows process. Only this process
links the Steinberg SDK or loads a third-party VST3 module. The parent uses
`HostSession` and the bounded codecs in `include/vsthost/`.

The main MSVC build enables `WANT_VST3` on Windows and builds independent x64
and Win32 ExternalProjects. It installs `plugins/RemoteVstHost64.exe` and
`plugins/32/RemoteVstHost32.exe`. `LMMS_VST3_SDK_ROOT` must name a complete
local SDK checkout; the build does not download or modify it. The implementation
uses SDK 3.8.0, with SDK examples, hosting examples and VSTGUI disabled. The
unchanged SDK Windows loader compiles as C++17; the host compiles as C++20.
`LMMS_BUILD_VST_HOST_ONLY=ON` configures the root project without Qt or DAW
dependencies. Set the generator platform to `x64` or `Win32` for that build.

## Implemented adapter behavior

- Scan every audio component CID; keep raw 16-byte IDs, vendor, categories,
  version and Unicode names. Controllers are not selectable audio entries.
- Load file or architecture-specific Windows bundle modules. Factory metadata
  and all control payloads have explicit count and size bounds.
- Initialize separate or combined component/controller objects, connect them,
  install the component handler, negotiate buses/sample precision and reverse
  the lifecycle on close. Owner-thread control operations run between audio
  calls, with a parent audio barrier for state/control requests.
- Convert interleaved float32 parent audio to preallocated planar float32 or
  float64 plugin buffers. Handle variable blocks, transport, bus silence,
  sample-offset parameter points, notes/poly pressure and MIDI controller maps.
  Preserve the original block sequence, size and offset in output feedback.
- Keep stable ParamIDs, controller gesture phases and current metadata. Suppress
  host setter echoes without suppressing callbacks from other plugin threads.
- Save component and independent controller state; restore component state into
  the controller before its own state. Reject oversized or invalid stream use.
- Own the native HWND/view/frame, pump messages, resize, hide/reopen and destroy
  the view before controller teardown. No HWND or native pointer crosses IPC.
- Service component reload, I/O rebuilding, parameter title/value refresh,
  latency refresh and MIDI assignment changes outside plugin callbacks. Reload
  releases the module, reopens the same CID and restores state/editor visibility.

The helper exposes SDK host application/message/attribute-list support and the
interfaces it implements; optional note expression and MIDI2 support are not
advertised. Other optional restart flags are reported to the parent. Bus totals
and latency are metadata for subsequent DAW graph integration; native adapter
support alone does not connect sidechains, multi-output routes or compensate
LMMS mixer paths. Catalog/UI/project integration belongs to S4/S5, external
Carla integration to S6 and commercial/fault-matrix validation to S7.

## Automated verification

Native CTest covers bounded factory faults, file/bundle scanning, exact CIDs,
independent instances, 32/64-bit samples, variable blocks, state, sample-offset
audio/events, MIDI reassignment, metadata reorder, latency and I/O changes,
complete module reload, and 50 fresh editor/helper lifetimes. Root CTest runs
the x64 parent against both helper ABIs. Parent module checks prove fixture
DLLs never load in the parent. `VST3_SUPPORT_PROGRESS.md` records stage results
and build-log locations. Listening, subjective commercial UI/DPI/focus review
and manual activation are `SKIPPED_MANUAL` by user instruction.
