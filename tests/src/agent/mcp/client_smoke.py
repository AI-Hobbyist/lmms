"""External official MCP SDK interoperability check (Python SDK 2.2.0).

Run with an AgentHarness executable; the child exits normally after its short lifetime.
"""
import argparse
import asyncio
import subprocess
import threading

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client


async def discover(endpoint, execute=False):
    async with streamable_http_client(endpoint) as (read, write):
        async with ClientSession(read, write, read_timeout_seconds=5) as session:
            initialized = await session.initialize()
            result = initialized.model_dump(by_alias=True, exclude_none=True)
            assert result["protocolVersion"] == "2025-11-25", result
            assert result["capabilities"].get("tools") == {}, result
            assert all(result["capabilities"].get(key) is None for key in ("resources", "prompts", "tasks"))
            tools = (await session.list_tools()).tools
            assert len(tools) == 159, len(tools)
            assert "agent.runScript" in {tool.name for tool in tools}
            assert not {tool.name for tool in tools} & {"agent.listCommands", "agent.getContext", "agent.diffPreview"}
            for tool in tools:
                schema = tool.model_dump(by_alias=True)["inputSchema"]
                assert schema["type"] == "object", tool.name
            await session.send_ping()
            print(f"Official SDK: initialize, {len(tools)} schemas, ping and JSON transport passed.", flush=True)
            if execute:
                await execution(session)


async def execution(session):
    async def call(name, arguments=None, ok=True):
        result = await session.call_tool(name, arguments or {})
        value = result.model_dump(by_alias=True)["structuredContent"]
        assert value["ok"] == ok and result.model_dump(by_alias=True)["isError"] == (not ok), value
        return value["data"]
    before = await call("track.list")
    script = {"steps": [
        {"cmd": "track.create", "args": {"type": "Instrument", "name": "MCP notes"}, "let": "track"},
        {"cmd": "clip.create", "args": {"track": "$track.index", "position": 0, "length": 192}, "let": "clip"},
        {"cmd": "midi.addNotes", "args": {"track": "$track.index", "clip": "$clip.index", "notes": [
            {"key": 48, "position": 0, "length": 48, "volume": 90},
            {"key": 52, "position": 48, "length": 48, "volume": 80}]}}]}
    preview = await call("agent.runScript", {"script": script, "dryRun": True})
    assert preview["diff"], preview
    assert await call("track.list") == before
    await call("agent.runScript", {"script": script})
    tracks = (await call("track.list"))["tracks"]
    track = len(tracks) - 1
    notes = await call("query.notes", {"track": track, "clip": 0})
    assert len(notes["notes"]) == 2, notes
    await call("midi.addNotes", {"track": track, "clip": 0, "notes": [{"key": -99}]}, ok=False)
    assert await call("query.notes", {"track": track, "clip": 0}) == notes
    await call("agent.runScript", {"script": {"steps": [
        {"cmd": "track.create", "args": {"type": "Instrument"}},
        {"assert": {"expr": False, "msg": "rollback check"}}]}}, ok=False)
    assert (await call("track.list"))["tracks"] == tracks
    await call("history.undo")
    assert await call("track.list") == before
    await call("history.redo")
    assert len((await call("query.notes", {"track": track, "clip": 0}))["notes"]) == 2
    await call("history.undo")
    parallel = await asyncio.gather(*(call("track.create", {"type": "Instrument", "name": f"Concurrent {i}"}) for i in range(8)))
    assert len({entry["index"] for entry in parallel}) == 8, parallel
    for _ in parallel:
        await call("history.undo")
    assert await call("track.list") == before
    print("Official SDK: writes, invalid arguments, dryRun, rollback, undo/redo and eight concurrent calls passed.", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("harness")
    parser.add_argument("--execute", action="store_true")
    args = parser.parse_args()
    process = subprocess.Popen([args.harness, "--mcp", "--duration", "10"], stdout=subprocess.PIPE, text=True)
    try:
        endpoint = None
        for line in process.stdout:
            print(line.rstrip(), flush=True)
            if line.startswith("http://127.0.0.1:"):
                endpoint = line.strip()
                break
        assert endpoint, "Harness did not provide a listening endpoint."
        def forward():
            for line in process.stdout:
                print(line.rstrip(), flush=True)
        reader = threading.Thread(target=forward, daemon=True)
        reader.start()
        asyncio.run(discover(endpoint, args.execute))
        assert process.wait(timeout=20) == 0, "Harness cleanup failed."
        reader.join(timeout=1)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
