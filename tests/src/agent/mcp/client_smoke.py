"""External official MCP SDK interoperability check (Python SDK 2.2.0).

Run with an AgentHarness executable; the child exits normally after its short lifetime.
"""
import argparse
import asyncio
import subprocess
import sys
import threading
import os
import secrets
import httpx2
import struct
import tempfile
import time
from pathlib import Path

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client


async def discover(endpoint, execute=False, render=False):
    async with httpx2.AsyncClient(headers={"Authorization": "Bearer " + os.environ["LMMS_MCP_TOKEN"]}) as client, \
            streamable_http_client(endpoint, http_client=client) as (read, write):
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
            if render:
                await render_cycle(session)


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


def check_wave(path):
    data = Path(path).read_bytes()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE", path
    offset, fmt, audio = 12, None, None
    while offset + 8 <= len(data):
        name, size = struct.unpack_from("<4sI", data, offset)
        chunk = data[offset + 8:offset + 8 + size]
        if name == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", chunk)
        if name == b"data":
            audio = chunk
        offset += 8 + size + (size & 1)
    assert fmt and audio, path
    kind, channels, rate, _, block, bits = fmt
    assert kind == 1 and bits in (16, 24), fmt
    width = bits // 8
    peak = max(abs(int.from_bytes(audio[i:i + width], "little", signed=True)) for i in range(0, len(audio), width)) / (1 << (bits - 1))
    duration = len(audio) / block / rate
    assert channels == 2 and 1 <= duration <= 8 and peak > 0.001, (fmt, duration, peak)
    return duration, peak, rate


async def render_cycle(session):
    async def call(name, args=None, ok=True):
        result = (await session.call_tool(name, args or {})).model_dump(by_alias=True)["structuredContent"]
        assert result["ok"] == ok, result
        return result["data"]
    async def finished(task):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            result = await call("export.status", {"task": task})
            if result["status"] != "running":
                assert result["status"] == "completed", result
                return result
            await asyncio.sleep(0.02)
        raise AssertionError("Audio task did not finish within eight seconds.")
    before = await call("track.list")
    music = await call("agent.runScript", {"script": "pop_chord_progression", "vars": {"bars": 1}, "seed": 42})
    track = music["vars"]["track"]["index"]
    await call("instrument.load", {"track": track, "plugin": "tripleoscillator"})
    preview = await call("render.preview", {"range": {"start": 0, "end": 192}})
    preview_path = preview["path"]
    try:
        preview_done = await finished(preview["task"])
        assert preview_done["bytes"] > 44
        preview_wave = check_wave(preview_path)
        with tempfile.TemporaryDirectory(prefix="lmms-mcp-") as folder:
            output = str(Path(folder) / "composition.wav")
            task = await call("export.audio", {"path": output, "format": "wav", "range": {"start": 0, "end": 192},
                "quality": {"bitDepth": 16, "sampleRate": 22050}})
            await finished(task["task"])
            output_wave = check_wave(output)
            assert output_wave[2] == 22050, output_wave
            cancelled_path = str(Path(folder) / "cancelled.wav")
            task = await call("export.audio", {"path": cancelled_path, "range": {"start": 0, "end": 192 * 4096}})
            await call("track.create", {"type": "Instrument"}, ok=False)
            cancel = await call("export.cancel", {"task": task["task"]})
            assert cancel["status"] == "cancelled", cancel
            assert not Path(cancelled_path).exists()
        await call("history.undo")  # Instrument load.
        await call("history.undo")  # Entire built-in composition.
        assert await call("track.list") == before
        timings = []
        for _ in range(50):
            started = time.perf_counter()
            await call("query.songSummary")
            timings.append((time.perf_counter() - started) * 1000)
        timings.sort()
        print(f"Official SDK: composition -> preview -> WAV export -> cancellation -> undo passed; "
              f"preview duration={preview_wave[0]:.2f}s peak={preview_wave[1]:.4f}, export rate={output_wave[2]}; "
              f"50 queries median={timings[25]:.2f}ms p95={timings[47]:.2f}ms max={timings[-1]:.2f}ms.", flush=True)
    finally:
        Path(preview_path).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("harness")
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--render", action="store_true")
    args = parser.parse_args()
    os.environ["LMMS_MCP_TOKEN"] = secrets.token_urlsafe(32)
    process = subprocess.Popen([args.harness, "--mcp", "--duration", "25" if args.render else "10"], stdout=subprocess.PIPE, text=True)
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
        asyncio.run(discover(endpoint, args.execute, args.render))
        example = Path(__file__).resolve().parents[4] / "data/agent/mcp_client.py"
        subprocess.run([sys.executable, str(example), endpoint], check=True, timeout=10)
        print("Documented Python connection example passed.", flush=True)
        assert process.wait(timeout=35) == 0, "Harness cleanup failed."
        reader.join(timeout=1)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
