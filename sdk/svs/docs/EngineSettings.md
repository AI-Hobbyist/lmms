# Global SVS engine settings (optional JSON extension)

The LMMS Settings dialog has separate VST and SVS categories. SVS exposes shared AI voicebank backend/device selectors and a separate named page for each installed engine.

An engine manifest may declare `name` and `engineType`: `ai`, `concatenative`, or `example`. The category suffix comes from that declaration; unknown types remain explicitly undeclared. SVSExample is a deterministic waveform synthesis example, not an AI model or a concatenative engine. The AI example page is only a host layout preview and does not register a synthesis engine. Its rendering-steps slider ranges from 1 to 100, defaults to 20, displays its value, and persists on confirmation. These demo values are not sent to a real engine.

A capabilities response may optionally add an `engineSettings` array:

```json
{
  "engineSettings": [
    {"id":"example.outputGain", "name":"Example engine output gain",
     "type":"float", "default":1, "min":0.1, "max":2, "step":0.1}
  ]
}
```

This extends the usual capability response; it does not replace its required fields. Descriptors use the existing parameter value types, ranges, choices, visibility and enablement rules. They are global engine options, not track/note parameters or curves. The host reuses its parameter editor and validator with an internal scope adapter; the mandatory ABI prefix and required capability schema remain unchanged; the optional query callback is appended in ABI 1.2.

On confirmation, values are persisted per engine in the LMMS configuration, passed as an `engineSettings` object in synthesis JSON, included in frozen render input/cache identity, and invalidate the affected live SVS clips. Cancel does not apply pending values. SVSExample demonstrates a real output-gain option; 0.5 produces half the sample amplitude of 1.0.

Backend/device options apply only to AI voicebanks. CPU is the actual available runtime backend. DirectML, LibTorch and Vulkan are selectable Coming soon placeholders to exercise device-picker enablement. Selecting CPU disables the picker and displays CPU. Other selections enable a picker containing CPU and actual Windows hardware graphics adapters enumerated through DXGI; software adapters are excluded. No GPU library is required. Saved device IDs use adapter LUIDs; if a device disappears, selection falls back to CPU. Placeholder choices never claim GPU acceleration: render snapshots continue to declare `computeBackend: "cpu"`, `computeDevice: "cpu"`.

## SDK ABI 1.2 interface

`svs_api.query_engine_settings(engine, context_json, &result_json)` is an optional append-only callback after `query_ranges`. Detect it using both table-size negotiation (`SVS_HAS_FIELD`) and a non-null pointer. `SVS_FEATURE_ENGINE_SETTINGS` identifies support. Major ABI and the mandatory prefix remain unchanged; ABI 1.0/1.1 plugins remain usable. C++ clients use `Engine::hasEngineSettings()` and `Engine::engineSettings(context)`; returned `String` owns the declaration and releases it through the engine's `release_string` callback. `SVS_UNSUPPORTED` is permitted for engines with no options.

The result is an engine-wide object, independent of the selected voice:

```json
{
  "schemaVersion": 1,
  "name": "AI engine example",
  "engineType": "ai",
  "engineSettings": [
    {"id":"renderSteps", "name":"Rendering steps", "type":"int",
     "default":20, "min":1, "max":100, "step":1}
  ]
}
```

`context_json` is an object containing current `engineSettings` values; engines may declare conditional controls with `visibleWhen`, `enabled` and `disabledReason`. The host does not call a shared-state setter: confirmed values are carried in each immutable `svs_snapshot.input_json` as `engineSettings`, with `computeBackend` and `computeDevice` describing the actual runtime selection. This prevents different sessions or export snapshots from depending on mutable engine state.

Name/type in the optional callback override manifest labels in Settings. Legacy engines without the callback may supply the optional `engineSettings` capability-response array; engines with neither declaration show no extra options. Neither the new callback nor the demonstration rendering-steps descriptor implements GPU acceleration.

## External catalogs and directory lists (ABI 1.3)

Installed engines have settings pages even when their catalog has zero voices. A `directory-list` descriptor has a string-array default and `maxItems` (1–128, default 128). LMMS provides editable paths, directory browsing, Add and Remove controls. Paths are user configuration, not project data. DiffSinger declares exactly `diffsinger.renderSteps` (integer 1–100, default 20) and `diffsinger.voicebankDirectories` (directory list).

The optional append-only `query_catalog(engine, context_json, &result_json)` follows `query_engine_settings`. Check table size, pointer and `SVS_FEATURE_CATALOG_QUERY`; older plugins keep using `catalog`. C++ clients use `Engine::hasCatalogQuery()` and `Engine::catalog(context)`. Returned text is released through `release_string`, including error text. The callback receives applied `engineSettings`, a host-owned `installations` array, `rescan`, and an optional `defaultVoicebankDirectory`. Empty directories use that host default; there is no built-in development `refs` path.

The response extends the usual `voices` catalog with `installations`, `diagnostics` and `catalogRevision`. Installation rows contain stable `id`, canonical `path` and content `fingerprint`; persist them in user configuration and return them on later queries. Explicit package IDs win. Otherwise a matching path retains its ID; a uniquely matching fingerprint preserves an ID after a move. Identical copies are deduplicated and conflicting explicit IDs are resolved by configured root priority with a diagnostic. Ambiguous moves require rebinding. Voices expose `version`/`contentFingerprint`; image content has a separate resource hash. A rescan atomically publishes a catalog snapshot, and already opened resources remain readable until close.

Apply saves directory changes and refreshes the catalog; Rescan reads applied settings. Cancel leaves them unchanged. Catalog changes refresh selectors and affect only tracks bound to the changed engine. Missing voices retain project IDs, lyrics and manual data. Directory-only changes do not invalidate unrelated synthesis. Catalog image IDs use the resource API; a catalog never grants arbitrary filesystem access.
