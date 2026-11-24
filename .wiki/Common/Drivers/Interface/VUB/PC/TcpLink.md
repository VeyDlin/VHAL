# VUB::PC::TcpLink

A TCP connection as a reliable byte stream, on Winsock (Windows) and POSIX sockets (Linux).
Header: `Common/Drivers/Interface/VUB/PC/TcpLink.h`. Namespace `VUB::PC`. See the [PC page](README) for the chain and the reopening rules.

## Traits

`LinkTraits().Reliable().FullDuplex().ReportsState().MaxPacket(256)`. A stream link, reliable. The link is up while a peer is connected.

Members: `Connect(host, port)`, `Listen(address, port)`, `BoundPort()`, `Close()`, `IsConnected()`, `IsConnecting()`, `Write(span)` (a
`Result<std::size_t>`), `Poll(Receiver)`, `SetStateSink(StateSink)`. It satisfies `VUB::StreamLink`, `VUB::PollingLink` and `VUB::ReportingLink`.

## Client and listener

- `Connect(host, port)` starts a connection. The host is an address or a name. A name is resolved with `getaddrinfo` in the call, which
  can wait for the resolver, so an address literal is the one to use where waiting matters. The connection completes in `Poll`: a
  non-blocking `connect` that returns in progress is finished by a `select` with a zero timeout and a read of `SO_ERROR`. The link is up
  from then.
- A name that resolves to several addresses is tried in the order the resolver gives them, up to eight. The addresses are copied into the
  link when `Connect` runs. An attempt that is refused, at once or in `Poll`, is closed and the next address is tried; `IsConnecting()`
  stays true meanwhile. The link reports failure only when the last address has refused: `IsConnecting()` turns false, `IsConnected()`
  stays false. `Connect` is then called again to try once more. On Windows `localhost` resolves to `::1` first, and the listener of
  `Listen` is IPv4 only, so the attempt on `::1` is refused and the one on `127.0.0.1` connects.
- `Connect` returns `notFound` when the name does not resolve, `noConnection` when at least one address got a socket and its connection
  was refused at once, and `ioError` when no address got a socket. The status does not depend on the order of the addresses: a socket
  that could not be made does not hide a refusal, and a refusal does not hide a socket that could not be made. On Windows a refused connect is seen in the except set of the `select`, and on Linux in the writable
  set with `SO_ERROR` set. A `select` that a signal interrupts (`EINTR`) is checked again in the next `Poll`.
- `Listen(address, port)` binds an IPv4 address (for example `127.0.0.1`) and listens with a backlog of one. A port of zero lets the system
  pick; `BoundPort()` reads it back. The listener accepts one peer in `Poll` whenever no peer is connected. When the peer goes away the
  listener keeps listening, so the next peer is accepted without a new `Listen`.

The connection socket is non-blocking and has `TCP_NODELAY`. On Windows, `WSAStartup` runs once, on first use, and `WSACleanup` is never
called: the start lasts until the process ends. On Linux the listener sets
`SO_REUSEADDR`, and a send does not raise `SIGPIPE` (`MSG_NOSIGNAL`).

## Reads and writes

- `Poll` calls `recv` for up to the receiver's room (at most 256 bytes at a time) and feeds what it reads. It never waits.
- `Write` calls `send` and returns the number of bytes the socket took. It returns zero while the socket is full, and when no peer is
  connected.

## States

- A connection that completes, or a peer that is accepted, reports up.
- A peer that closes (a `recv` that returns zero), a read or a write that fails, or `Close` reports down and drops the connection. A
  listener drops the peer and keeps listening.
- `Connect` and `Listen` close what was open before they start.

## Snippet

The snippet below is compiled by `TestVubPcWiki.cpp`. It does not run in a test.

```cpp
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/PC/TcpLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>

void OnNetPayload(const VUB::Meta&, std::span<const uint8>) {
}

inline uint32 NetClockUs() {
	return 0;
}

using NetStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::PC::TcpLink>()
		.MaxPayload(64)
		.ReceiveRing(256)
		.OnReceive<&OnNetPayload>()
		.Clock<&NetClockUs>();
}>;

// The listening side accepts one peer in Process. Port 0 would let the system pick; BoundPort reads it back.
void RunListener() {
	VUB::PC::TcpLink link;
	NetStack stack(link);
	if (link.Listen("127.0.0.1", 5000) == ResultStatus::ok) {
		stack.Process();
	}
}

// The client connects; the connection completes in Process, and the link is up from then.
void RunClient() {
	VUB::PC::TcpLink link;
	NetStack stack(link);
	if (link.Connect("127.0.0.1", 5000) == ResultStatus::ok) {
		stack.Process();
	}
}
```

## Test

`TestVubPcTcp.cpp` runs two stacks on `127.0.0.1`, a listener and a client, with COBS framing on Windows and on Linux. It checks that a packet
crosses each way with the same bytes, that a client that closes is seen as down by the listener, that the client connects again and the
listener accepts it without a new `Listen`, and that a connection to a port with nothing listening is refused within 200 `Process` steps
(the link is not connecting and is not up), and that `Connect` then comes up on a listener. It also connects to the name `localhost` on a
listener of `127.0.0.1`, and reports a refused `localhost` connection as not up. `TestVubPcTcpReliable.cpp`
carries three packets each way with `VUB::Reliable` over the same link.
