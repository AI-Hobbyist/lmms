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
