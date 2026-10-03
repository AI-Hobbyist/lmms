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
