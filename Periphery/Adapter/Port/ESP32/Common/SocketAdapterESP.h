#pragma once
#include <Adapter/SocketAdapter.h>
#include <cerrno>
#include <cstdio>
#include <cstring>


using ASOCKET = class SocketAdapterESP;


// TCP, UDP and TLS client sockets on the ESP-IDF lwIP stack and esp-tls. The link underneath is whatever network
// interface is up; the adapter does not bring it up.
//
// The lwip_* names are used on purpose: the POSIX names may be redefined as macros by the lwIP configuration.
// Every connection is blocking with SO_RCVTIMEO and SO_SNDTIMEO set to the timeout, so a single Read() or Write()
// never waits longer than that.
class SocketAdapterESP : public SocketAdapter {
protected:
	int socketHandle = -1;
	esp_tls_t *tls = nullptr;


public:
	SocketAdapterESP() = default;

	SocketAdapterESP(const SocketAdapterESP&) = delete;
	SocketAdapterESP& operator=(const SocketAdapterESP&) = delete;

	~SocketAdapterESP() override {
		Close();
	}


	ResultStatus Open(const char *host, uint16 port) override {
		if (!IsStackStarted()) {
			return ResultStatus::noInit;
		}

		if (host == nullptr || host[0] == '\0') {
			return ResultStatus::invalidParameter;
		}

		Close();

		if (parameters.protocol == Protocol::Tls) {
			return OpenTls(host, port);
		}

		return OpenSocket(host, port);
	}


	ResultStatus Close() override {
		if (tls != nullptr) {
			esp_tls_conn_destroy(tls);
			tls = nullptr;
		}

		if (socketHandle >= 0) {
			lwip_close(socketHandle);
			socketHandle = -1;
		}

		return ResultStatus::ok;
	}


	bool IsOpen() const override {
		return socketHandle >= 0 || tls != nullptr;
	}


	Result<Ipv4Address> Resolve(const char *host) override {
		if (!IsStackStarted()) {
			return ResultStatus::noInit;
		}

		if (host == nullptr || host[0] == '\0') {
			return ResultStatus::invalidParameter;
		}

		addrinfo hints = {};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		addrinfo *info = nullptr;
		if (lwip_getaddrinfo(host, nullptr, &hints, &info) != 0 || info == nullptr) {
			return ResultStatus::notFound;
		}

		// sin_addr is in network byte order, so its bytes in memory are the octets as written.
		Ipv4Address result;
		const sockaddr_in *address = reinterpret_cast<const sockaddr_in *>(info->ai_addr);
		std::memcpy(result.bytes, &address->sin_addr, Ipv4Address::size);
		lwip_freeaddrinfo(info);

		return result;
	}


	ResultStatus WriteByteArray(const uint8 *buffer, uint32 size) override {
		if (!IsOpen()) {
			return ResultStatus::noInit;
		}

		uint32 sent = 0;
		while (sent < size) {
			if (tls != nullptr) {
				ssize_t result = esp_tls_conn_write(tls, buffer + sent, size - sent);
				// After a send timeout the TLS state is unknown, so a retry would mix records: report it instead.
				if (result == ESP_TLS_ERR_SSL_WANT_WRITE || result == ESP_TLS_ERR_SSL_WANT_READ) {
					return ResultStatus::timeout;
				}
				if (result <= 0) {
					return ResultStatus::writeError;
				}
				sent += static_cast<uint32>(result);
			} else {
				ssize_t result = lwip_send(socketHandle, buffer + sent, size - sent, 0);
				if (result < 0) {
					if (errno == EAGAIN || errno == EWOULDBLOCK) {
						return ResultStatus::timeout;
					}
					return ResultStatus::writeError;
				}
				if (result == 0) {
					return ResultStatus::writeError;
				}
				sent += static_cast<uint32>(result);
			}
		}

		return ResultStatus::ok;
	}


	Result<uint32> ReadByteArray(uint8 *buffer, uint32 size) override {
		if (!IsOpen()) {
			return ResultStatus::noInit;
		}

		if (size == 0) {
			return ResultStatus::invalidParameter;
		}

		if (tls != nullptr) {
			ssize_t result = esp_tls_conn_read(tls, buffer, size);
			if (result > 0) {
				return static_cast<uint32>(result);
			}
			if (result == 0) {
				return ResultStatus::connectionClosed;
			}
			if (result == ESP_TLS_ERR_SSL_WANT_READ || result == ESP_TLS_ERR_SSL_WANT_WRITE) {
				return ResultStatus::timeout;
			}
			return ResultStatus::readError;
		}

		ssize_t result = lwip_recv(socketHandle, buffer, size, 0);
		if (result > 0) {
			return static_cast<uint32>(result);
		}
		if (result == 0) {
			return ResultStatus::connectionClosed;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return ResultStatus::timeout;
		}
		return ResultStatus::readError;
	}


protected:
	ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		Close();
		return AfterInitialization();
	}


private:
	// Any lwIP call made before esp_netif_init() starts the TCP/IP task fails an assert and resets the chip.
	// esp_netif_deinit() is documented to return ESP_ERR_NOT_SUPPORTED once the stack is initialised and
	// ESP_ERR_INVALID_STATE before that. Deinitialisation is not implemented, so the call changes nothing.
	// If it is ever implemented, this probe has to be replaced. Checked against ESP-IDF 5.5.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
	#warning "VHAL: SocketAdapterESP::IsStackStarted() probes esp_netif_deinit(); check that it still has no side effects"
#endif
	static bool IsStackStarted() {
		return esp_netif_deinit() == ESP_ERR_NOT_SUPPORTED;
	}


	static timeval MakeTimeval(uint32 milliseconds) {
		timeval value = {};
		value.tv_sec = static_cast<time_t>(milliseconds / 1000);
		value.tv_usec = static_cast<suseconds_t>((milliseconds % 1000) * 1000);
		return value;
	}


	bool SetTimeouts(int fd) const {
		timeval limit = MakeTimeval(timeout);
		if (lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit)) != 0) {
			return false;
		}
		return lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit)) == 0;
	}


	ResultStatus OpenSocket(const char *host, uint16 port) {
		addrinfo hints = {};
		hints.ai_family = AF_INET;
		hints.ai_socktype = (parameters.protocol == Protocol::Udp) ? SOCK_DGRAM : SOCK_STREAM;

		char service[6] = {};
		snprintf(service, sizeof(service), "%u", static_cast<unsigned int>(port));

		addrinfo *info = nullptr;
		if (lwip_getaddrinfo(host, service, &hints, &info) != 0 || info == nullptr) {
			return ResultStatus::notFound;
		}

		int fd = lwip_socket(info->ai_family, info->ai_socktype, info->ai_protocol);
		if (fd < 0) {
			lwip_freeaddrinfo(info);
			return ResultStatus::noConnection;
		}

		ResultStatus status = ConnectWithTimeout(fd, info->ai_addr, info->ai_addrlen);
		lwip_freeaddrinfo(info);

		if (status == ResultStatus::ok && !SetTimeouts(fd)) {
			status = ResultStatus::noConnection;
		}

		if (status != ResultStatus::ok) {
			lwip_close(fd);
			return status;
		}

		socketHandle = fd;
		return ResultStatus::ok;
	}


	// The socket is non-blocking only while connecting, so the timeout can be applied to the connect itself.
	ResultStatus ConnectWithTimeout(int fd, const sockaddr *address, socklen_t length) {
		int flags = lwip_fcntl(fd, F_GETFL, 0);
		if (flags < 0 || lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
			return ResultStatus::noConnection;
		}

		ResultStatus status = ResultStatus::ok;
		if (lwip_connect(fd, address, length) != 0) {
			status = (errno == EINPROGRESS) ? WaitForConnect(fd) : ResultStatus::noConnection;
		}

		if (lwip_fcntl(fd, F_SETFL, flags) < 0 && status == ResultStatus::ok) {
			status = ResultStatus::noConnection;
		}

		return status;
	}


	ResultStatus WaitForConnect(int fd) {
		fd_set writeSet;
		FD_ZERO(&writeSet);
		FD_SET(fd, &writeSet);

		timeval limit = MakeTimeval(timeout);
		int ready = lwip_select(fd + 1, nullptr, &writeSet, nullptr, &limit);
		if (ready == 0) {
			return ResultStatus::timeout;
		}
		if (ready < 0) {
			return ResultStatus::noConnection;
		}

		// A refused connection also makes the socket writable, so SO_ERROR is what tells the two apart.
		int error = 0;
		socklen_t length = sizeof(error);
		if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0) {
			return ResultStatus::noConnection;
		}

		return ResultStatus::ok;
	}


	ResultStatus OpenTls(const char *host, uint16 port) {
		esp_tls_cfg_t cfg = {};
		cfg.timeout_ms = static_cast<int>(timeout);

		if (parameters.caCertificate != nullptr) {
			cfg.cacert_buf = reinterpret_cast<const unsigned char *>(parameters.caCertificate);
			// mbedTLS counts the terminating zero of PEM text.
			cfg.cacert_bytes = static_cast<unsigned int>(std::strlen(parameters.caCertificate) + 1);
		} else {
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
			cfg.crt_bundle_attach = esp_crt_bundle_attach;
#else
			return ResultStatus::notSupported;
#endif
		}

		tls = esp_tls_init();
		if (tls == nullptr) {
			return ResultStatus::outOfMemory;
		}

		if (esp_tls_conn_new_sync(host, static_cast<int>(std::strlen(host)), port, &cfg, tls) != 1) {
			esp_tls_conn_destroy(tls);
			tls = nullptr;
			return ResultStatus::noConnection;
		}

		int fd = -1;
		if (esp_tls_get_conn_sockfd(tls, &fd) != ESP_OK || !SetTimeouts(fd)) {
			Close();
			return ResultStatus::noConnection;
		}

		return ResultStatus::ok;
	}
};
