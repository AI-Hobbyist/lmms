# Local HTTP MCP

Build with `WANT_AGENT=ON` and `WANT_AGENT_MCP=ON` (both default ON).
`WANT_AGENT_MCP=OFF` removes MCP sources and the Qt Network dependency while
retaining the local command API and JSON scripts. `WANT_AGENT=OFF` disables both.
The service is disabled at runtime until you enable it in LMMS Settings / General.

Before launching LMMS, provide a random local bearer token in `LMMS_MCP_TOKEN`.
It must contain 16–512 printable ASCII characters without spaces. The same token
is used by your external client. The settings page shows the environment variable
name, never the token. Configuration is kept in LMMS's local configuration:

```xml
<agentMcp enabled="false" port="0" tokenEnv="LMMS_MCP_TOKEN"/>
```

Choose a fixed port, or leave 0 to assign an available port. Click **Apply server
settings**, then **Copy connection address**. Saving the dialog also applies
changed enable/port settings. The Apply button takes effect immediately, including
when you subsequently cancel the dialog. Missing tokens and occupied ports show
an error; after correcting the environment/configuration, retry Apply (changing an
inherited environment variable requires restarting LMMS).

The endpoint is always `http://127.0.0.1:<actual-port>/mcp`. Add
`Authorization: Bearer <token>` to the external client's HTTP headers. Clients
must support Streamable HTTP and MCP 2025-11-25. The automatically verified client
is the official Python MCP SDK 2.2.0; no model provider, cloud account or model SDK
is needed inside LMMS.

The [Python connection example](mcp_client.py) uses SDK 2.2.0 and its `httpx2`
dependency. Run it with the copied endpoint and the same `LMMS_MCP_TOKEN` environment
variable. With no flags it discovers tools and queries the project. `--compose`
adds a chord track and loads TripleOscillator; `--preview` renders the first bar;
`--export FILE.wav` exports the current project to an existing parent directory.
Existing files are rejected unless a caller explicitly passes `overwrite:true`.
Use `history.undo` to undo the instrument load and then the complete composition.
Exported files are outside project undo. Preview files are caller-owned: play them
in your chosen player and remove them when finished.

Tools are generated from the same registry as the [command reference](command-reference.md).
`tools/call` returns a text representation and `structuredContent` containing
`{ok,data,error?}`; business failures set `isError:true`. Use `agent.runScript`
with `dryRun:true` to preview an atomic multi-command edit. `export.audio` and
`render.preview` return task ids and local paths; poll `export.status` and use
`export.cancel` for cancellation. Finished/cancelled tasks remain queryable, up to
64 recent tasks. Mutating commands are rejected while rendering, except cancellation.

## Discovery tools

The read-only discovery tools return stable handles and absolute local paths for
existing load commands:

- `instrument.listPlugins` → `instrument.load` (`plugin`, optional `subKey`, `path`,
  `classid`, `architecture`); `effect.listAvailable(kind=instrument)` remains available
  and unchanged.
- `preset.list` → `instrument.loadPreset` (`path`). Entries with
  `loadableBy:["pluginPreset"]` (for example `.xiz`) belong to a plugin's own preset
  browser and are not accepted by `instrument.loadPreset`.
- `sample.list` → `sample.setFile` / `import.sampleToTrack` (`path`).
- `fs.listRoots` returns drives, home and known LMMS directories. `fs.listDirectory`
  lists direct children of an explicitly supplied absolute path; it is non-recursive,
  never writes, and does not follow directory symlinks while scanning.

`svs.listVoices` and `svc.listModels` report asynchronous catalog state. Retry while
`catalog.scanning` is true or an engine `status` says discovery is still running;
voicebank and model lists may be empty on the first call. List tools are read-only,
do not change the project, and are outside undo.

## Mixer channel tools

`mixer.addChannel` appends a mixer channel and returns the same channel detail object
as `mixer.getChannel`; the new channel is routed to the master channel automatically
and can be named in the same call. Route a track to it with `track.setMixerChannel`.
`mixer.removeChannel` protects channel 0 (master) and refuses channels that a track
still uses; pass `force:true` to reroute those tracks to the master channel first.
`mixer.moveChannel` reorders one channel a single slot left or right; the master stays
at index 0 and cannot be moved. These three commands change the mixer structure, so when
the GUI is running they rebuild the mixer view and refresh the per-track channel ranges
before returning; without a GUI they only touch the model. All three are project-undoable
and support `dryRun`. Send, volume, mute, solo, name and color changes continue to use
the existing `mixer.set*` commands.

SVS and SVC tracks have no effect chain of their own. Route such a track to a mixer
channel with `track.setMixerChannel` (instrument, sample, SVS and SVC tracks are all
accepted, and `track.get` reports `mixerChannel` for each of them) and add effects on
that channel (`effect.add` with `owner: "song/channel:N"`).

`mixer.getLevels` reports the current peak level of one channel (`channel`) or every
channel as linear values plus `peakDb` (dBFS, floored at −120). Peaks come from the
audio engine, so they read 0 while the transport is stopped and update during playback
and export; use them to drive `mix.gainStaging` instead of guessing levels.

`track.get` / `track.list` and `clip.get` / `clip.list` report SVS and SVC tracks and
clips as `type: "svs"` / `"svc"` instead of `"unknown"`; the dedicated
`svs.getTrack` / `svc.getTrack` payloads remain the richer view.

## Time and tick units

Positions and lengths are song ticks, not beats or milliseconds. `DefaultTicksPerBar`
is 192, so in 4/4 one bar is **192 ticks**, one quarter-note beat is 48 ticks and one
16th step is 12 ticks. Commands that take `bar`/`bars` convert with that value, while
`clip.create`/`midi.*`/`svs.*` positions and lengths are raw ticks. A 10-second phrase
at 120 BPM is 5 bars, i.e. 960 ticks; `getSongInfo`/`agent.runScript` report the same
`ticksPerBar` so scripts can compute from it instead of assuming a value.

## SVS editing tools

`svs.createTrack` (optionally with `pluginId`/`voiceId`) and `svs.createClip` create
SVS project data. After `svs.setVoice`, poll `svs.getTrack` until
`capabilitiesReady` is true before using lyrics, phoneme or parameter commands.
`svs.getNotes` returns stable note ids; update and remove notes by id. `svs.setLyrics`
uses the same word/ideograph tokenization as the piano-roll batch-lyrics dialog and
preserves manual pronunciation and phoneme overrides. `svs.getPronunciation` is a
read-only preview. `svs.getCurves` lists editable parameter curves; `svs.setCurve`
replaces a declared curve object or removes it with `curve:null` and an `id`.
The piano-roll pitch line is an editor curve (`svs.pitch`, absolute semitones) rather
than a voicebank parameter, so it has its own command: `svs.setPitchCurve` writes
`points:[{tick,value}]` (values are offsets from the sustaining note by default, or
absolute pitches with `relative:false`), supports `linear`/`hermite`/`step`
interpolation, and clears the curve with `clear:true`.
`svs.synthesize` and `svs.cancelSynthesis` start or cancel asynchronous work and are
rejected inside a project transaction; poll `svs.getStatus` for `status`,
`audioAvailable` and cache/segment information.
Project edits from SVS commands are part of project undo; synthesis results are not.

## SVC tools

`svc.createTrack` optionally takes a `selection` object (`engine_id`, `model_id`,
optional `weight_id`/`speaker_id`/`parameters`). `svc.setModel` normalizes the
selection through the engine capability contract: it validates the model, weight and
speaker, fills parameter defaults and rejects unavailable or out-of-range values.
Selection never contains connection tokens. `svc.setSourceFile` decodes a local
audio file into the clip and is a blocking, project-undoable operation.
`svc.setChunkConfig` updates the silence/length/forced thresholds and invalidates
affected clips. `svc.convert` and `svc.cancel` are asynchronous and rejected inside a
project transaction; poll `svc.getStatus` for `status`, `progress`,
`conversionComplete`, `conversionFailed` and `cacheReferences`. `svc.getTrack`
returns the normalized selection, engine status, model parameters, chunk config and
clip summaries.

One local MCP session is supported: initialization replaces the previous session.
The client sends the returned `MCP-Session-Id`, the negotiated protocol header and
the initialized notification before using tools. Stopping invalidates the session
and cancels queued commands and the render started by this service. Independent
local renders remain available. Disconnecting a client does not cancel committed
edits or retry writes; reconnect by initializing again and query project/task state
before deciding on another action. An oversized response or lost HTTP reply does
not imply that a write was rolled back.

HTTP POST requests use UTF-8 JSON, `Content-Type: application/json`, and an Accept
header containing both `application/json` and `text/event-stream`. Responses are
JSON; accepted notifications/responses receive 202 with no body. GET/DELETE return
405. No resources, prompts, sampling, experimental tasks, SSE events or chat UI are
declared. Host must match the actual localhost/127.0.0.1 endpoint and port. An Origin
header, when present, must match the same HTTP endpoint origin. Non-browser clients
may omit Origin. All methods require the bearer token.

Limits: 16 KiB headers, 4 MiB body, 32 connections, 16 queued tools, 8 MiB response,
10 seconds for network reads/writes. Scripts keep the documented local execution
limits. HTTP requests use Content-Length; chunked requests and pipelining are
rejected. Send cancellation notifications for queued requests; running project
transactions finish at a safe boundary. Audio cancellation uses the export tool.

Automated validation is in `Mcp*Test` and
`tests/src/agent/mcp/client_smoke.py HARNESS --execute --render`. On Windows, copy
AgentHarness.exe to an isolated directory as lmms.exe with current native plugins
in its plugins directory, as described in [README](README.md). Keep Qt and native
runtime DLLs on PATH. The test creates its own temporary bearer token and cleans
its generated audio. It checks discovery, edits, rollback, undo, concurrent calls,
non-silent PCM data, export cancellation and query timings. Human UI inspection and
listening are skipped by user instruction, not reported as passed. Qt5/Linux/macOS
execution requires those build environments and is recorded separately from the
Windows Qt6 results.

Protocol references: [transport](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports),
[lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle),
[tools](https://modelcontextprotocol.io/specification/2025-11-25/server/tools).
