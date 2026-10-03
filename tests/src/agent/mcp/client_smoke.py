"""External official MCP SDK interoperability check (Python SDK 2.2.0).

Run with an AgentHarness executable; the child exits normally after its short lifetime.
"""
import argparse
import asyncio
import subprocess
import threading

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client


async def discover(endpoint):
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("harness")
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
        asyncio.run(discover(endpoint))
        assert process.wait(timeout=20) == 0, "Harness cleanup failed."
        reader.join(timeout=1)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
