#pragma once
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#error "VUB::PC::TcpLink is written for Windows and Linux only"
#endif

namespace VUB::PC {


// A TCP connection as a reliable byte stream, over Winsock on Windows and POSIX sockets on Linux. The socket is non-blocking and sets
// TCP_NODELAY. Connect starts a connection to a host (an IPv4 or IPv6 address, or a name); the connection completes in Poll, and the link
// is up from then. A name that resolves to several addresses is tried in the order the resolver gives them, up to eight: each address
// that refuses the connection is replaced by the next one, and the link fails only when the last one has refused. Listen takes one peer:
// it binds the address (IPv4 only) and accepts a peer in Poll whenever no peer is connected, so a peer that reconnects is accepted
// without another Listen. A port of zero lets the system pick, and BoundPort reads it back.
//
// The link is up while a peer is connected. When the peer goes away, or a read or a write fails, the link reports down and drops the
// connection. A listening link keeps listening; a connecting link must call Connect again.
class TcpLink {
public:
#if defined(_WIN32)
	using Socket = SOCKET;
	static constexpr Socket noSocket = INVALID_SOCKET;
#else
	using Socket = int;
	static constexpr Socket noSocket = -1;
#endif


	static constexpr LinkTraits traits = LinkTraits().Reliable().FullDuplex().ReportsState().MaxPacket(256);


private:
	static constexpr std::size_t maxAddresses = 8;


	StateSink sink;
	Socket listener = noSocket;
	Socket connection = noSocket;
	bool connecting = false;
	bool up = false;
	std::array<sockaddr_storage, maxAddresses> addresses{};
	std::array<socklen_t, maxAddresses> addressLengths{};
	std::size_t addressCount = 0;
	std::size_t nextAddress = 0;


public:
	TcpLink() = default;


	TcpLink(const TcpLink&) = delete;
	TcpLink& operator=(const TcpLink&) = delete;


	~TcpLink() {
		CloseSockets();
	}


	// Starts a connection to host and port. Returns ok when the connection is started (it may still be completing; the link is up once
	// Poll sees it complete). Returns notFound when the name does not resolve, and an error when no address of it can be connected at
	// once: ioError when no socket could be made, noConnection when every address refused. The addresses are kept until the next Connect,
	// so Poll tries the next one when an attempt fails, without allocating.
	ResultStatus Connect(const std::string& host, std::uint16_t port) {
		Close();
		if (!WinsockReady()) {
			return ResultStatus::notAvailable;
		}

		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		addrinfo* found = nullptr;
		std::string service = std::to_string(port);
		if (getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || found == nullptr) {
			return ResultStatus::notFound;
		}

		addressCount = 0;
		for (addrinfo* entry = found; entry != nullptr && addressCount < maxAddresses; entry = entry->ai_next) {
			if (entry->ai_addr == nullptr || entry->ai_addrlen > sizeof(sockaddr_storage)) {
				continue;
			}

			std::memcpy(&addresses[addressCount], entry->ai_addr, entry->ai_addrlen);
			addressLengths[addressCount] = static_cast<socklen_t>(entry->ai_addrlen);
			addressCount++;
		}

		freeaddrinfo(found);
		nextAddress = 0;
		return StartNextAttempt();
	}


	// Listens on address and port for one peer. Returns ok when the port is bound; the peer is accepted in Poll.
	ResultStatus Listen(const std::string& address, std::uint16_t port) {
		Close();
		if (!WinsockReady()) {
			return ResultStatus::notAvailable;
		}

		sockaddr_in bindAddress{};
		bindAddress.sin_family = AF_INET;
		bindAddress.sin_port = htons(port);
		if (inet_pton(AF_INET, address.c_str(), &bindAddress.sin_addr) != 1) {
			return ResultStatus::invalidParameter;
		}

		Socket created = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (created == noSocket) {
			return ResultStatus::ioError;
		}

#if !defined(_WIN32)
		int reuse = 1;
		setsockopt(created, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
		if (bind(created, reinterpret_cast<sockaddr*>(&bindAddress), static_cast<socklen_t>(sizeof(bindAddress))) != 0 || listen(created, 1) != 0) {
			CloseSocket(created);
			return ResultStatus::addressAlreadyInUse;
		}

		SetNonBlocking(created);
		listener = created;
		return ResultStatus::ok;
	}


	// The port the listener is bound to, or of the connection. Zero when neither exists.
	std::uint16_t BoundPort() const {
		Socket source = listener;
		if (source == noSocket) {
			source = connection;
		}

		if (source == noSocket) {
			return 0;
		}

		sockaddr_in address{};
		socklen_t length = static_cast<socklen_t>(sizeof(address));
		if (getsockname(source, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
			return 0;
		}

		return ntohs(address.sin_port);
	}


	// Drops the connection and stops listening. Reports down when the link was up.
	void Close() {
		CloseSockets();
		SetUp(false);
	}


	bool IsConnected() const {
		return up;
	}


	// Whether a connection started by Connect is still completing. It is false once Poll has seen it come up or fail.
	bool IsConnecting() const {
		return connecting;
	}


	// The stack gives its sink when it is built, which may be after Open. A stack starts with its link down, so a link that is up
	// reports it into the sink at once.
	void SetStateSink(StateSink value) {
		sink = value;
		if (IsConnected()) {
			sink.Report(true);
		}
	}


	// Completes a connection, accepts a peer, and reads what the peer sent, up to the receiver's room. Never waits.
	void Poll(Receiver receiver) {
		if (connecting) {
			CheckConnect();
			return;
		}

		if (listener != noSocket && connection == noSocket) {
			Accept();
		}

		if (!up || connection == noSocket) {
			return;
		}

		std::size_t room = receiver.Room();
		if (room == 0) {
			return;
		}

		std::array<std::uint8_t, 256> buffer{};
		std::size_t limit = room < buffer.size() ? room : buffer.size();
		int got = static_cast<int>(recv(connection, reinterpret_cast<char*>(buffer.data()), static_cast<int>(limit), 0));
		if (got > 0) {
			receiver.Feed(std::span<const uint8>(buffer.data(), static_cast<std::size_t>(got)));
			return;
		}

		if (got == 0 || !WouldBlock()) {
			Drop();
		}
	}


	// Sends what the socket takes at once. Returns zero while the socket is full, or when no peer is connected.
	Result<std::size_t> Write(std::span<const uint8> bytes) {
		if (!up || connection == noSocket || bytes.empty()) {
			return std::size_t{0};
		}

#if defined(MSG_NOSIGNAL)
		constexpr int sendFlags = MSG_NOSIGNAL;
#else
		constexpr int sendFlags = 0;
#endif
		int sent = static_cast<int>(send(connection, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), sendFlags));
		if (sent > 0) {
			return static_cast<std::size_t>(sent);
		}

		if (!WouldBlock()) {
			Drop();
		}

		return std::size_t{0};
	}


private:
	void SetUp(bool value) {
		if (up == value) {
			return;
		}

		up = value;
		sink.Report(value);
	}


	// Drops the peer: its connection closes and the link reports down. The listener stays.
	void Drop() {
		CloseConnection();
		SetUp(false);
	}


	void CloseConnection() {
		if (connection != noSocket) {
			CloseSocket(connection);
			connection = noSocket;
		}

		connecting = false;
	}


	void CloseSockets() {
		CloseConnection();
		if (listener != noSocket) {
			CloseSocket(listener);
			listener = noSocket;
		}
	}


	// Starts a connection to the next candidate address that can be started. A candidate whose socket cannot be made, or that refuses at
	// once, is skipped. Returns ok when a connection is started (up at once, or completing), or the error of the last candidate skipped
	// when none is left. Connecting is false when no connection is started.
	ResultStatus StartNextAttempt() {
		ResultStatus status = ResultStatus::noConnection;
		while (nextAddress < addressCount) {
			std::size_t index = nextAddress;
			nextAddress++;
			Socket created = socket(static_cast<int>(addresses[index].ss_family), SOCK_STREAM, IPPROTO_TCP);
			if (created == noSocket) {
				status = ResultStatus::ioError;
				continue;
			}

			SetNonBlocking(created);
			SetNoDelay(created);
			int result = connect(created, reinterpret_cast<const sockaddr*>(&addresses[index]), addressLengths[index]);
			if (result != 0 && !ConnectInProgress()) {
				CloseSocket(created);
				status = ResultStatus::noConnection;
				continue;
			}

			connection = created;
			connecting = result != 0;
			if (!connecting) {
				SetUp(true);
			}

			return ResultStatus::ok;
		}

		return status;
	}


	// Looks at a connection that is completing: when the socket is writable, the connection is up. A failed connect is closed, and the
	// next candidate address is tried; the link fails when none is left. Winsock reports a failed connect in the except set and not in the
	// writable set, so the except set is watched there too. Linux reports a failed connect as writable, and SO_ERROR tells the two apart.
	void CheckConnect() {
		fd_set writable;
		fd_set failed;
		FD_ZERO(&writable);
		FD_ZERO(&failed);
		FD_SET(connection, &writable);
#if defined(_WIN32)
		FD_SET(connection, &failed);
		fd_set* failedSet = &failed;
#else
		fd_set* failedSet = nullptr;
#endif
		timeval wait{};
		int ready = select(static_cast<int>(connection) + 1, nullptr, &writable, failedSet, &wait);
#if !defined(_WIN32)
		// A signal interrupted the select: the connect is still completing, and the next Poll checks it again.
		if (ready < 0 && errno == EINTR) {
			return;
		}
#endif
		if (ready == 0) {
			return;
		}

		bool refused = ready < 0;
#if defined(_WIN32)
		refused = refused || FD_ISSET(connection, &failed) != 0;
#endif
		if (!refused) {
			int error = 0;
			socklen_t length = static_cast<socklen_t>(sizeof(error));
			refused = getsockopt(connection, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 || error != 0;
		}

		if (!refused) {
			connecting = false;
			SetUp(true);
			return;
		}

		CloseConnection();
		StartNextAttempt();
	}


	// Takes one peer from the listener, if one is waiting.
	void Accept() {
		sockaddr_storage peerAddress{};
		socklen_t length = static_cast<socklen_t>(sizeof(peerAddress));
		Socket peer = accept(listener, reinterpret_cast<sockaddr*>(&peerAddress), &length);
		if (peer == noSocket) {
			return;
		}

		SetNonBlocking(peer);
		SetNoDelay(peer);
		connection = peer;
		SetUp(true);
	}


	static void CloseSocket(Socket value) {
#if defined(_WIN32)
		closesocket(value);
#else
		::close(value);
#endif
	}


	static void SetNonBlocking(Socket value) {
#if defined(_WIN32)
		u_long mode = 1;
		ioctlsocket(value, FIONBIO, &mode);
#else
		int flags = fcntl(value, F_GETFL, 0);
		fcntl(value, F_SETFL, flags | O_NONBLOCK);
#endif
	}


	static void SetNoDelay(Socket value) {
		int flag = 1;
		setsockopt(value, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
	}


	// Whether the last socket call would have waited, so it is to be tried again.
	static bool WouldBlock() {
#if defined(_WIN32)
		return WSAGetLastError() == WSAEWOULDBLOCK;
#else
		return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
	}


	// Whether the last connect call started a connection that completes later.
	static bool ConnectInProgress() {
#if defined(_WIN32)
		return WSAGetLastError() == WSAEWOULDBLOCK;
#else
		return errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EINTR;
#endif
	}


	// Starts Winsock once for the process, on first use. The start is not undone: WSACleanup is never called, so Winsock stays started
	// until the process ends. On Linux there is nothing to start.
	static bool WinsockReady() {
#if defined(_WIN32)
		static const bool ready = []() -> bool {
			WSADATA data{};
			return WSAStartup(MAKEWORD(2, 2), &data) == 0;
		}();
		return ready;
#else
		return true;
#endif
	}
};

}
