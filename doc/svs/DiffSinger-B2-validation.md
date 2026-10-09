# B2 global compute policy

Status: PASS (Windows x64, 2026-10-09).

The host freezes requested/effective backend and device, policy revision, runtime
version, stage constraints and fallback diagnostics into AI synthesis snapshots.
Traditional and non-AI engines keep their existing CPU input identity. Unsupported
AI engines explicitly resolve to CPU. The shared runtime verifies a real DML node
on the selected DXGI LUID before it is offered as available.

CPU disables the device selector. DirectML lists successful probes; a saved missing
device stays visible as unavailable instead of silently selecting another GPU.
Editing settings does not publish a policy. Apply persists it and advances the
revision only when backend/device changes. AI clips are invalidated and resubmitted;
queued/running AI jobs are cancelled and old completions discarded. Captured,
preparing and active AI exports fail explicitly on a policy change. Traditional
exports are unaffected.

Workers consume value snapshots and never read ConfigManager. Immediately before
cache lookup, the selected device is resolved/probed again; a missing device chooses
CPU with a reason. Cache content identity includes the effective route/runtime/stage
constraints and excludes request-only fields and policy revision. Cached clip
provenance preserves the effective policy for missing-engine recovery without a
new GPU inference.

The shared C++ SDK path wrapper now handles C++20 char8_t and C++17 UTF-8 path
strings consistently. A failed host build diagnosed this difference. A second
failed link exposed duplicate moc ownership; the new SVS QObject is now excluded
from the host moc list, following existing SVS module ownership.

Evidence is recorded under validation/B2-*. Logs retain failed attempts for
diagnosis. B3 real DiffSinger GPU synthesis and B4 independent delivery remain
separate milestones.

## Required validation

- B2-host-QtTest.txt: 4 passed / 0 failed; real shared AI clip and export use
  AMD Radeon 780M LUID dxgi:00000000:000148ca. Actual inference profile reports
  1 DML node / 0 CPU nodes. CPU rendering, Apply invalidation, frozen export,
  effective-route cache recovery and a real nonexistent LUID fallback pass.
- The independent restarted host child reads the persisted DirectML backend,
  exact LUID and revision: 3 passed / 0 failed.
- Running and queued policy jobs cancel on Apply; a deliberately late-returning
  result is discarded. A queued traditional job finishes normally. Captured AI
  exports fail while traditional exports keep their state.
- B2-native-QtTest.txt: 3 passed / 0 failed on native Windows Qt and the development
  theme. CPU disables the selector; DirectML excludes CPU and selects probed GPUs.
  Editing without save keeps configuration unchanged. Reopening preserves the LUID.
  A saved absent LUID is selected but disabled. Real exposed windows were stabilized,
  captured and closed; B2-native-compute-settings.png and B2-native-missing-device.png
  were inspected: readable text, normal theme and no clipped selectors.
- B2-regression-QtTest.txt: 6 passed / 0 failed for original export preparation,
  cancellation/budget, cache identity/recovery and cache directory/legacy reads.
  The synthetic AI fixture manifest was temporarily renamed in place and restored
  in finally because these original tests select the first legacy example voice.
- Original deployment was rebuilt: build/Release/lmms.exe,
  build/Release/svs/SVSDiffSinger/SVSDiffSinger.dll and
  build/Release/svs/SVSExample/SVSExample.dll. The shared AI fixture remains at
  build/Release/svs/SVSComputeExample with the host-required category/platform/
  architecture fields. No replacement deployment directories were used.

B3 full trained DiffSinger GPU execution and B4 full regression/delivery are pending.
B1 worker isolation/fault coverage is retained. The native test harness requires
QT_PLUGIN_PATH from the refreshed QTDIR; missing-plugin startup was diagnosed and
fixed through the native Windows Qt deployment path.