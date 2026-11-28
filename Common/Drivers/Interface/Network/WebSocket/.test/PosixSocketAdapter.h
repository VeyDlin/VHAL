#pragma once
#include <Adapter/SocketAdapter.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>


// SocketAdapter over POSIX sockets, for the host tests only. IPv4 and plain TCP. Tls is not available here
// (Open returns notSupported), so a wss:// test never silently runs over plain TCP. The file is included only where the
// POSIX socket headers exist.
class PosixSocketAdapter : public SocketAdapter {
private:
	int descriptor = -1;


public:
	~PosixSocketAdapter() {
		Close();
	}


	ResultStatus Open(const char* host, uint16 hostPort) override {
		Close();
		if (parameters.protocol == SocketAdapter::Protocol::Tls) {
			return ResultStatus::notSupported;
		}

		if (host == nullptr || host[0] == '\0' || parameters.connectTimeoutMs == 0) {
			return ResultStatus::invalidParameter;
		}

		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* found = nullptr;
		std::string portText = std::to_string(hostPort);
		if (getaddrinfo(host, portText.c_str(), &hints, &found) != 0 || found == nullptr) {
			return ResultStatus::notFound;
		}

		int fd = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
		if (fd < 0) {
			freeaddrinfo(found);
			return ResultStatus::noConnection;
		}

		// SO_SNDTIMEO bounds the blocking connect, so it carries the connect timeout until the connection is up.
		timeval connectLimit = ToTimeval(parameters.connectTimeoutMs);
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &connectLimit, sizeof(connectLimit));

		if (connect(fd, found->ai_addr, found->ai_addrlen) != 0) {
			::close(fd);
			freeaddrinfo(found);
			return ResultStatus::noConnection;
		}

		int one = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		freeaddrinfo(found);
		descriptor = fd;
		ApplyIoTimeouts();
		return ResultStatus::ok;
	}


	void SetIoTimeout(uint32 val) override {
		SocketAdapter::SetIoTimeout(val);
		ApplyIoTimeouts();
	}


	ResultStatus Close() override {
		if (descriptor >= 0) {
			::close(descriptor);
			descriptor = -1;
		}

		return ResultStatus::ok;
	}


	bool IsOpen() const override {
		return descriptor >= 0;
	}


	Result<Ipv4Address> Resolve(const char* host) override {
		if (host == nullptr || host[0] == '\0') {
			return ResultStatus::invalidParameter;
		}

		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* found = nullptr;
		if (getaddrinfo(host, nullptr, &hints, &found) != 0 || found == nullptr) {
			return ResultStatus::notFound;
		}

		uint32 raw = reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr.s_addr;
		freeaddrinfo(found);
		return Ipv4Address(static_cast<uint8>(raw), static_cast<uint8>(raw >> 8),
			static_cast<uint8>(raw >> 16), static_cast<uint8>(raw >> 24));
	}


	ResultStatus WriteByteArray(const uint8* buffer, uint32 size) override {
		if (descriptor < 0) {
			return ResultStatus::noInit;
		}

		uint32 sent = 0;
		while (sent < size) {
			ssize_t count = send(descriptor, buffer + sent, size - sent, MSG_NOSIGNAL);
			if (count > 0) {
				sent += static_cast<uint32>(count);
			} else if (count < 0 && errno == EINTR) {
				continue;
			} else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				return ResultStatus::timeout;
			} else {
				return ResultStatus::writeError;
			}
		}

		return ResultStatus::ok;
	}


	Result<uint32> ReadByteArray(uint8* buffer, uint32 size) override {
		if (descriptor < 0) {
			return ResultStatus::noInit;
		}

		if (size == 0) {
			return ResultStatus::invalidParameter;
		}

		// SO_RCVTIMEO cannot express 0 (it waits forever), so a zero I/O timeout is MSG_DONTWAIT on this call.
		int flags = (ioTimeout == 0) ? MSG_DONTWAIT : 0;
		ssize_t count = recv(descriptor, buffer, size, flags);
		if (count > 0) {
			return static_cast<uint32>(count);
		}

		if (count == 0) {
			return ResultStatus::connectionClosed;
		}

		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return ResultStatus::timeout;
		}

		return ResultStatus::readError;
	}


protected:
	ResultStatus Initialization() override {
		return ResultStatus::ok;
	}


private:
	static timeval ToTimeval(uint32 milliseconds) {
		timeval value{};
		value.tv_sec = static_cast<time_t>(milliseconds / 1000);
		value.tv_usec = static_cast<suseconds_t>((milliseconds % 1000) * 1000);
		return value;
	}


	// Writes wait for the I/O timeout, or the connect timeout when that is 0.
	void ApplyIoTimeouts() {
		if (descriptor < 0) {
			return;
		}

		timeval readLimit = ToTimeval(ioTimeout);
		setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &readLimit, sizeof(readLimit));
		uint32 writeMs = (ioTimeout > 0) ? ioTimeout : parameters.connectTimeoutMs;
		timeval writeLimit = ToTimeval(writeMs);
		setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &writeLimit, sizeof(writeLimit));
	}
};
