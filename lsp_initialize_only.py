#!/usr/bin/env python3
"""Initialize gopls without ever sending textDocument/didOpen."""

import json
import os
from pathlib import Path
import selectors
import subprocess
import sys
import time


if len(sys.argv) != 4:
    raise SystemExit("usage: lsp_initialize_only.py ROOT GOPLS MARKER")

root = Path(sys.argv[1]).resolve()
gopls = Path(sys.argv[2]).resolve()
marker = Path(sys.argv[3]).resolve()
root_uri = root.as_uri()

proc = subprocess.Popen(
    [str(gopls), "serve"],
    cwd=root,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=sys.stderr,
    env=os.environ.copy(),
)
assert proc.stdin is not None and proc.stdout is not None

selector = selectors.DefaultSelector()
selector.register(proc.stdout, selectors.EVENT_READ)
buffer = bytearray()


def send(message):
    body = json.dumps(message, separators=(",", ":")).encode()
    proc.stdin.write(b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
    proc.stdin.flush()


def read_message(deadline):
    while time.monotonic() < deadline:
        split = buffer.find(b"\r\n\r\n")
        if split >= 0:
            headers = bytes(buffer[:split]).decode("ascii")
            length = None
            for line in headers.split("\r\n"):
                key, sep, value = line.partition(":")
                if sep and key.lower() == "content-length":
                    length = int(value.strip())
                    break
            if length is None:
                raise RuntimeError("LSP frame has no Content-Length")
            end = split + 4 + length
            if len(buffer) >= end:
                body = bytes(buffer[split + 4 : end])
                del buffer[:end]
                return json.loads(body)
        events = selector.select(min(1.0, max(0.0, deadline - time.monotonic())))
        if not events:
            if proc.poll() is not None:
                raise RuntimeError(f"gopls exited early with status {proc.returncode}")
            continue
        chunk = os.read(proc.stdout.fileno(), 65536)
        if not chunk:
            raise RuntimeError("gopls closed stdout")
        buffer.extend(chunk)
    return None


def handle_request(message):
    if "id" not in message or "method" not in message:
        return
    if message["method"] == "workspace/configuration":
        result = [None for _ in message.get("params", {}).get("items", [])]
    elif message["method"] == "workspace/workspaceFolders":
        result = [{"uri": root_uri, "name": root.name}]
    else:
        result = None
    send({"jsonrpc": "2.0", "id": message["id"], "result": result})


def await_response(response_id, deadline):
    while True:
        message = read_message(deadline)
        if message is None:
            raise TimeoutError(f"timed out waiting for response {response_id}")
        handle_request(message)
        if message.get("id") == response_id and "method" not in message:
            return message


try:
    send(
        {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "processId": None,
                "clientInfo": {"name": "go-cgo-macos-poc", "version": "1"},
                "rootUri": root_uri,
                "workspaceFolders": [{"uri": root_uri, "name": root.name}],
                "capabilities": {
                    "workspace": {"configuration": True, "workspaceFolders": True}
                },
            },
        }
    )
    reply = await_response(1, time.monotonic() + 60)
    if "error" in reply:
        raise RuntimeError(f"initialize failed: {reply['error']}")
    send({"jsonrpc": "2.0", "method": "initialized", "params": {}})

    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        if marker.exists() and marker.stat().st_size > 0:
            break
        message = read_message(min(deadline, time.monotonic() + 1.0))
        if message is not None:
            handle_request(message)
    if not marker.exists() or marker.stat().st_size == 0:
        raise RuntimeError("marker was not created during workspace initialization")
finally:
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

raise SystemExit(0)
