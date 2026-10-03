"""Connect to a running LMMS loopback server with the official Python MCP SDK 2.2.0.

python mcp_client.py http://127.0.0.1:5333/mcp
python mcp_client.py http://127.0.0.1:5333/mcp --compose --preview --export composition.wav
"""
import argparse
import asyncio
import json
import os
from pathlib import Path
from urllib.parse import urlsplit

import httpx2
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client


async def run(args):
    token = os.environ["LMMS_MCP_TOKEN"]
    async with httpx2.AsyncClient(headers={"Authorization": "Bearer " + token}) as http, \
            streamable_http_client(args.endpoint, http_client=http) as (read, write), \
            ClientSession(read, write, read_timeout_seconds=30) as client:
        await client.initialize()
        print("Available tools:", len((await client.list_tools()).tools))
        async def call(name, arguments=None):
            result = (await client.call_tool(name, arguments or {})).model_dump(by_alias=True)
            value = result["structuredContent"]
            if not value["ok"]:
                raise RuntimeError(json.dumps(value["error"], ensure_ascii=False))
            return value["data"]
        async def wait(task):
            while True:
                status = await call("export.status", {"task": task})
                if status["status"] != "running":
                    if status["status"] != "completed":
                        raise RuntimeError(json.dumps(status, ensure_ascii=False))
                    return status
                await asyncio.sleep(0.1)
        print(json.dumps(await call("query.songSummary"), indent=2, ensure_ascii=False))
        if args.compose:
            music = await call("agent.runScript", {"script": "pop_chord_progression", "seed": 42})
            track = music["vars"]["track"]["index"]
            await call("instrument.load", {"track": track, "plugin": "tripleoscillator"})
        if args.preview:
            preview = await call("render.preview", {"range": {"start": 0, "end": 192}})
            print("Preview:", (await wait(preview["task"]))["path"])
        if args.export:
            export = await call("export.audio", {"path": str(Path(args.export).resolve()), "format": "wav"})
            print("Export:", (await wait(export["task"]))["path"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("endpoint")
    parser.add_argument("--compose", action="store_true")
    parser.add_argument("--preview", action="store_true")
    parser.add_argument("--export")
    args = parser.parse_args()
    endpoint = urlsplit(args.endpoint)
    if endpoint.scheme != "http" or endpoint.hostname not in ("127.0.0.1", "localhost") or \
            endpoint.username is not None or endpoint.password is not None or endpoint.path != "/mcp" or \
            endpoint.query or endpoint.fragment or not endpoint.port:
        parser.error("Use the copied local HTTP /mcp endpoint with its actual port.")
    asyncio.run(run(args))
