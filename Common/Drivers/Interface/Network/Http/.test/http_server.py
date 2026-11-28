#!/usr/bin/env python3
"""Loopback HTTP server for the HttpClient integration tests. Standard library only.

The first line on stdout is the bound port. The server runs until its standard input ends: the test closes the pipe when
it finishes, and the server exits with it.

Routes:
  /length        Content-Length body
  /chunked       chunked body with chunk extensions and trailers, sent in pieces
  /close         HTTP/1.1 body read to close (Connection: close)
  /http10        HTTP/1.0 response, body read to close
  /count         number of connections accepted so far (keep-alive checks)
  /interim       100 Continue and 103 Early Hints before the final 200
  /redirect      301 with Location: /length (must not be followed by the client)
  /echo          POST: echoes the body and reports the Authorization header it received
  /stall         never answers within the client timeout
  /sse           chunked text/event-stream, chat-completion shape, odd-sized slices with delays
  /sse-crlf      the same events with CRLF line endings and tiny slices
"""
import argparse
import asyncio
import os
import sys
import threading

connections = 0

SSE_EVENTS = [
    '{"choices":[{"index":0,"delta":{"role":"assistant"}}]}',
    '{"choices":[{"index":0,"delta":{"content":"Привет"}}]}',
    '{"choices":[{"index":0,"delta":{"content":", мир"}}]}',
    '{"choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}',
    "[END]",
]

SLICE_SIZES = [1, 2, 3, 5, 8, 13, 7]


def sse_payload(lineEnd):
    parts = []
    for event in SSE_EVENTS:
        parts.append("data: " + event + lineEnd + lineEnd)
    return "".join(parts).encode("utf-8")


async def pause():
    await asyncio.sleep(0.002)


async def write_chunked_pieces(writer, pieces):
    for piece in pieces:
        writer.write(("%x\r\n" % len(piece)).encode("ascii") + piece + b"\r\n")
        await writer.drain()
        await pause()
    writer.write(b"0\r\n\r\n")
    await writer.drain()


async def write_sse(writer, payload, sizes):
    head = (
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
    )
    writer.write(head.encode("ascii"))
    await writer.drain()
    pieces = []
    position = 0
    index = 0
    while position < len(payload):
        size = sizes[index % len(sizes)]
        pieces.append(payload[position:position + size])
        position += size
        index += 1
    await write_chunked_pieces(writer, pieces)


def simple(status, headers, body):
    lines = ["HTTP/1.1 " + status + "\r\n"]
    for name, value in headers:
        lines.append(name + ": " + value + "\r\n")
    lines.append("Content-Length: " + str(len(body)) + "\r\n")
    lines.append("\r\n")
    return "".join(lines).encode("ascii") + body


async def route(method, path, headers, body, writer):
    """Writes the response for one request. Returns True when the connection stays open."""
    if method == "HEAD" and path == "/length":
        # Same headers as GET, no body: a HEAD response never carries one.
        writer.write(b"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\n")
        await writer.drain()
        return True

    if path == "/length":
        writer.write(simple("200 OK", [("Content-Type", "text/plain")], b"hello world"))
        return True

    if path == "/chunked":
        pieces = [b"The quick brown ", b"fox jumps ", b"over the lazy dog."]
        head = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        writer.write(head.encode("ascii"))
        await writer.drain()
        for index, piece in enumerate(pieces):
            extension = ";part=" + str(index) + ";quoted=\"yes\"" if index % 2 == 0 else ""
            writer.write(("%x%s\r\n" % (len(piece), extension)).encode("ascii") + piece + b"\r\n")
            await writer.drain()
            await pause()
        writer.write(b"0;last=1\r\nX-Checksum: abc\r\nX-Note: trailer\r\n\r\n")
        await writer.drain()
        return True

    if path == "/close":
        writer.write(b"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nclose-delimited body")
        await writer.drain()
        return False

    if path == "/http10":
        writer.write(b"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nhttp10 body")
        await writer.drain()
        return False

    if path == "/count":
        writer.write(simple("200 OK", [], str(connections).encode("ascii")))
        return True

    if path == "/interim":
        wire = (
            "HTTP/1.1 100 Continue\r\n\r\n"
            "HTTP/1.1 103 Early Hints\r\nLink: </style.css>; rel=preload\r\n\r\n"
            "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"
        )
        writer.write(wire.encode("ascii"))
        await writer.drain()
        return True

    if path == "/redirect":
        writer.write(b"HTTP/1.1 301 Moved Permanently\r\nLocation: /length\r\nContent-Length: 0\r\n\r\n")
        await writer.drain()
        return True

    if path == "/echo":
        seen = headers.get("authorization", "")
        contentType = headers.get("content-type", "")
        writer.write(simple("200 OK", [
            ("X-Authorization-Seen", seen),
            ("X-Content-Type-Seen", contentType),
        ], body))
        await writer.drain()
        return True

    if path == "/stall":
        await asyncio.sleep(3)
        writer.write(simple("200 OK", [], b"late"))
        await writer.drain()
        return False

    if path == "/sse":
        await write_sse(writer, sse_payload("\n"), SLICE_SIZES)
        return True

    if path == "/sse-crlf":
        await write_sse(writer, sse_payload("\r\n"), [1, 1, 2, 1, 3])
        return True

    writer.write(simple("404 Not Found", [], b"no such route"))
    await writer.drain()
    return True


def parse_head(raw):
    text = raw.decode("latin-1")
    lines = text.split("\r\n")
    requestLine = lines[0].split(" ")
    method = requestLine[0]
    path = requestLine[1] if len(requestLine) > 1 else "/"
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            name, value = line.split(":", 1)
            headers[name.strip().lower()] = value.strip()
    return method, path, headers


async def handle(reader, writer):
    global connections
    connections += 1
    try:
        while True:
            try:
                raw = await reader.readuntil(b"\r\n\r\n")
            except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, ConnectionError):
                break

            method, path, headers = parse_head(raw)
            length = int(headers.get("content-length", "0") or "0")
            body = await reader.readexactly(length) if length else b""
            keepOpen = await route(method, path, headers, body, writer)
            await writer.drain()
            if not keepOpen:
                break
    except (ConnectionError, OSError):
        pass
    finally:
        try:
            writer.close()
        except OSError:
            pass


def watch_standard_input():
    # Returns when the test closes the pipe, and then the whole server ends with the test.
    sys.stdin.buffer.read()
    os._exit(0)


async def main(port):
    server = await asyncio.start_server(handle, "127.0.0.1", port)
    boundPort = server.sockets[0].getsockname()[1]
    sys.stdout.write(str(boundPort) + "\n")
    sys.stdout.flush()
    async with server:
        await server.serve_forever()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=0)
    return parser.parse_args()


if __name__ == "__main__":
    arguments = parse_args()
    threading.Thread(target=watch_standard_input, daemon=True).start()
    asyncio.run(main(arguments.port))
