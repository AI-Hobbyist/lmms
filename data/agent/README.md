# Local command and script tools

Build with WANT_AGENT=ON and BUILD_TESTING=ON. The optional AgentHarness executable
uses QCoreApplication and the same command bus as LMMS. It opens no editor or chat
window and connects to no model service.

The generated [command reference](command-reference.md) contains the current
schemas. Regenerate it with AgentHarness --manual after changing command schemas.

AgentHarness accepts:

- --tools: print MCP tool definitions generated from the running registry.
- --manual: print the generated command reference, including parameter schemas.
- --builtin NAME: execute one of the nine scripts in scripts/.
- --script FILE: execute a local JSON script file.
- --vars JSON: override script variables with a JSON object.
- --seed INT: use a reproducible 32-bit seed.
- --dry-run: execute project edits and roll them back, returning their diff.

For example, run AgentHarness --builtin pop_chord_progression --seed 42 --dry-run.
The result contains variables, the last command result, step counts, a bounded
trace, seed, LMMS version and elapsed time. Failures include the step location.

On Windows, native plugin DLLs import the LMMS executable's exports. To test scripts
that load those DLLs, copy AgentHarness.exe to an isolated build directory as
lmms.exe and put the required current plugin DLLs in its plugins directory. Keep
Qt and vcpkg runtime DLLs on PATH. This follows the native integration test setup;
ordinary script regression requires no instrument DLLs.

Project scripts form one undo transaction. Any command, assertion or expression
failure rolls back all edits. Nested built-in calls share the step budget and
random stream. Loops exclude their upper bound and restore the previous loop
variable. Positions and lengths use native ticks: a quarter note is 48 ticks,
C0 is native key 0, and C4 is key 48. Command results are objects, so use
$track.index and $clip.index rather than the whole returned object.

File writes, configuration and playback require a standalone one-command script.
Their dry-run behavior comes from the underlying command; they are outside project
undo. Multi-step scripts cannot combine these operations with project mutations.
Nested agent.runScript/history calls are rejected; use call for built-in scripts.

The generation assets create MIDI clips. Load an instrument separately to hear
them. humanize_groove and scale_snap require an existing clip, and scale_snap
requires a configured octave scale. mix_gain_staging uses a caller-supplied measured
peak, not an internal loudness analyzer. render_preview returns a temporary WAV
path and export task; the caller plays and removes the file. The harness waits up
to 60 seconds for an export before cancelling it.

MCP clients discover tools through tools/list. Optional local agent metadata and
diffPreview helpers are excluded from the MCP tool catalog. This layer generates
tool definitions; the optional loopback HTTP transport belongs to phase B.
