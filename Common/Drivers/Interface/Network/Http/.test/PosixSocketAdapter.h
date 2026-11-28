#pragma once
#include <Adapter/SocketAdapter.h>
#include <Utilities/Result/Result.h>
#include <cerrno>
#include <cstring>
#include <functional>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


// SocketAdapter over POSIX TCP for the host tests. Tls and Udp are not available here, so Open() returns
// notSupported for them. The timeout is applied with SO_RCVTIMEO and SO_SNDTIMEO at Open(). The file is included only
// where the POSIX socket headers exist.
class PosixSocketAdapter : public SocketAdapter {
public:
	// Runs before every read, so a test can defragment a heap between reads.
	std::function<void()> beforeRead;

private:
	int descriptor = -1;

public:
	PosixSocketAdapter() = default;


	~PosixSocketAdapter() override {
		Close();
	}


	ResultStatus Open(const char *host, uint16 port) override {
		Close();
		if (parameters.protocol != SocketAdapter::Protocol::Tcp) {
			return ResultStatus::notSupported;
		}

		if (host == nullptr || host[0] == '\0' || parameters.connectTimeoutMs == 0) {
			return ResultStatus::invalidParameter;
		}

		Result<Ipv4Address> address = Resolve(host);
		if (address.IsErr()) {
			return address.Error();
		}

		sockaddr_in peer;
		std::memset(&peer, 0, sizeof(peer));
		peer.sin_family = AF_INET;
		peer.sin_port = htons(port);
		std::memcpy(&peer.sin_addr, address.Value().bytes, Ipv4Address::size);

		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			return ResultStatus::noConnection;
		}

		// SO_SNDTIMEO bounds the blocking connect, so it carries the connect timeout until the connection is up.
		timeval connectLimit = ToTimeval(parameters.connectTimeoutMs);
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &connectLimit, sizeof(connectLimit));

		if (connect(fd, reinterpret_cast<sockaddr *>(&peer), sizeof(peer)) != 0) {
			int error = errno;
			::close(fd);
			if (error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS || error == ETIMEDOUT) {
				return ResultStatus::timeout;
			}

			return ResultStatus::noConnection;
		}

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


	Result<Ipv4Address> Resolve(const char *host) override {
		addrinfo hints;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		addrinfo *found = nullptr;
		if (getaddrinfo(host, nullptr, &hints, &found) != 0 || found == nullptr) {
			return ResultStatus::notFound;
		}

		const sockaddr_in *ipv4 = reinterpret_cast<const sockaddr_in *>(found->ai_addr);
		const uint8 *raw = reinterpret_cast<const uint8 *>(&ipv4->sin_addr);
		Ipv4Address address(raw[0], raw[1], raw[2], raw[3]);
		freeaddrinfo(found);
		return address;
	}


	ResultStatus WriteByteArray(const uint8 *buffer, uint32 size) override {
		if (descriptor < 0) {
			return ResultStatus::noInit;
		}

		uint32 sent = 0;
		while (sent < size) {
			ssize_t count = send(descriptor, buffer + sent, size - sent, MSG_NOSIGNAL);
			if (count < 0) {
				if (errno == EINTR) {
					continue;
				}

				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					return ResultStatus::timeout;
				}

				return ResultStatus::writeError;
			}

			sent += static_cast<uint32>(count);
		}

		return ResultStatus::ok;
	}


	Result<uint32> ReadByteArray(uint8 *buffer, uint32 size) override {
		if (beforeRead) {
			beforeRead();
		}

		if (descriptor < 0) {
			return ResultStatus::noInit;
		}

		if (size == 0) {
			return ResultStatus::invalidParameter;
		}

		// SO_RCVTIMEO cannot express 0 (it waits forever), so a zero I/O timeout is MSG_DONTWAIT on this call.
		int flags = (ioTimeout == 0) ? MSG_DONTWAIT : 0;
		while (true) {
			ssize_t count = recv(descriptor, buffer, size, flags);
			if (count > 0) {
				return static_cast<uint32>(count);
			}

			if (count == 0) {
				return ResultStatus::connectionClosed;
			}

			if (errno == EINTR) {
				continue;
			}

			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return ResultStatus::timeout;
			}

			return ResultStatus::readError;
		}
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
