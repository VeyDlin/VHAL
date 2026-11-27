#!/usr/bin/env python3
# Local WebSocket server for the live tests of the WebSocket client. Uses the websockets library as the reference peer.
# The first line on stdout is the bound port. The server runs until its standard input ends: the test closes the pipe when
# it finishes, and the server exits with it.
#
# Routes (path, query ignored):
#   /echo            echo every text and binary message
#   /ping            send a ping, wait for the client's pong, then send "pong-received" or "pong-missing"
#   /close/<code>    wait for one message, then close with <code> and reason "bye"
#   /fragmented      send one fragmented text message ("Hel" + "lo") and one fragmented binary message
#   /big/<n>         send one binary message of n bytes
#   /auth            needs "Authorization: Bearer test-token" (401 otherwise); answers "auth:<header value>"
#   /subprotocol     answers with the chosen subprotocol as text ("none" when none was chosen)
#   /events          event-shaped JSON text frames, then a normal close
#   /reject          answers the handshake with 401 and a JSON body (Content-Length)
#   /forbidden       answers the handshake with 403 and a plain text body (Content-Length)
import os
import sys
import threading

from websockets.datastructures import Headers
from websockets.http11 import Response
from websockets.sync.server import serve


JSON_401 = b'{"error":"invalid api key"}'
TEXT_403 = b"forbidden: model not allowed"


def process_request(connection, request):
    if request.path.startswith("/reject"):
        headers = Headers([("Content-Type", "application/json"), ("Content-Length", str(len(JSON_401)))])
        return Response(401, "Unauthorized", headers, JSON_401)

    if request.path.startswith("/forbidden"):
        headers = Headers([("Content-Type", "text/plain"), ("Content-Length", str(len(TEXT_403)))])
        return Response(403, "Forbidden", headers, TEXT_403)

    if request.path.startswith("/auth"):
        if request.headers.get("Authorization") != "Bearer test-token":
            headers = Headers([("Content-Length", "0")])
            return Response(401, "Unauthorized", headers, b"")

    return None


def select_subprotocol(connection, subprotocols):
    if "chat" in subprotocols:
        return "chat"
    return None


def handler(ws):
    path = ws.request.path

    if path.startswith("/ping"):
        event = ws.ping(b"server-ping")
        answered = event.wait(5)
        ws.send("pong-received" if answered else "pong-missing")
        return

    if path.startswith("/close/"):
        code = int(path.split("/")[2])
        ws.recv()
        ws.close(code=code, reason="bye")
        return

    if path.startswith("/fragmented"):
        ws.send(["Hel", "lo"])
        ws.send([b"\x00\x01", b"\x02"])
        return

    if path.startswith("/big/"):
        size = int(path.split("/")[2])
        ws.send(b"x" * size)
        return

    if path.startswith("/auth"):
        ws.send("auth:" + ws.request.headers.get("Authorization", ""))
        return

    if path.startswith("/subprotocol"):
        ws.send(ws.subprotocol if ws.subprotocol else "none")
        return

    if path.startswith("/events"):
        ws.send('{"type":"event.created","id":"e-1"}')
        ws.send('{"type":"event.partial","text":"hello","is_final":false,"speech_final":false}')
        ws.send('{"type":"event.partial","text":"hello world","is_final":true,"speech_final":true}')
        ws.close(code=1000, reason="")
        return

    for message in ws:
        ws.send(message)


def watch_standard_input():
    # Returns when the test closes the pipe, and then the whole server ends with the test.
    sys.stdin.buffer.read()
    os._exit(0)


def main():
    threading.Thread(target=watch_standard_input, daemon=True).start()
    server = serve(handler, "127.0.0.1", 0, process_request=process_request,
                   select_subprotocol=select_subprotocol, subprotocols=["chat"], max_size=None)
    port = server.socket.getsockname()[1]
    sys.stdout.write(str(port) + "\n")
    sys.stdout.flush()
    server.serve_forever()


if __name__ == "__main__":
    main()
