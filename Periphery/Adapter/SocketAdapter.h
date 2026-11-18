#pragma once
#include "IAdapter.h"
#include <Adapter/Utilities/NetworkTypes.h>

#define VHAL_SOCKET_ADAPTER


// One client connection: TCP, UDP or TLS over TCP.
// It does not know which link carries the data (Wi-Fi, Ethernet, a modem), so code written against SocketAdapter&
// is reused over all of them. The link must be up before Open().
//
// The class is not a template for the same reason as WiFiAdapter: an external module that runs its own TCP/IP stack
// implements the same interface.
class SocketAdapter : public IAdapter {
public:
	enum class Protocol { Tcp, Udp, Tls };

	struct Parameters {
		Protocol protocol = Protocol::Tcp;
		// Protocol::Tls only. PEM text of the CA certificate that signs the server; it must stay valid until Open()
		// returns. nullptr verifies the server against the certificate set built into the port.
		const char *caCertificate = nullptr;
		// Limit for Open() in milliseconds: the TCP connect and, for Protocol::Tls, the TLS handshake. Must be above 0,
		// otherwise Open() returns invalidParameter. It does not limit Read() or Write(); see SetIoTimeout().
		uint32 connectTimeoutMs = 10000;
	};


protected:
	Parameters parameters;
	uint32 ioTimeout = 10000;


public:
	// Closes the current connection, if any.
	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	// Limit for one Read() and for one Write(), in milliseconds. A port applies it to an open connection at once, so it
	// can change between reads. Before the first Open() it is applied by Open().
	// 0 makes Read() non-blocking: it returns timeout at once when no byte is buffered, and never waits.
	// Write() has no non-blocking form, because a partly sent buffer cannot be resumed; with 0 it waits up to
	// Parameters::connectTimeoutMs instead.
	virtual void SetIoTimeout(uint32 val) {
		ioTimeout = val;
	}


	// Sets Parameters::connectTimeoutMs and the I/O timeout to val. This is the single timeout of earlier versions,
	// so existing callers keep their behaviour. Use SetIoTimeout() to change the I/O limit alone.
	void SetTimeout(uint32 val) {
		parameters.connectTimeoutMs = val;
		SetIoTimeout(val);
	}


	// host is a name or a dotted IPv4 address. For Udp it fixes the peer for Write() and Read().
	virtual ResultStatus Open(const char *host, uint16 port) = 0;
	virtual ResultStatus Close() = 0;
	virtual bool IsOpen() const = 0;

	virtual Result<Ipv4Address> Resolve(const char *host) = 0;


	// Sends the whole buffer or returns an error.
	virtual ResultStatus WriteByteArray(const uint8 *buffer, uint32 size) = 0;

	// Returns the number of bytes received, from 1 to size. Nothing within the timeout is ResultStatus::timeout;
	// the peer closing the connection is ResultStatus::connectionClosed.
	virtual Result<uint32> ReadByteArray(uint8 *buffer, uint32 size) = 0;


	template <typename DataType>
	inline ResultStatus Write(const DataType& data) {
		return WriteByteArray(reinterpret_cast<const uint8*>(&data), sizeof(DataType));
	}


	template <typename DataType>
	inline ResultStatus WriteArray(const DataType *buffer, uint32 size) {
		return WriteByteArray(reinterpret_cast<const uint8*>(buffer), sizeof(DataType) * size);
	}


	inline ResultStatus WriteString(const char *string) {
		uint32 size = 0;
		while (string[size]) {
			size++;
		}

		return WriteByteArray(reinterpret_cast<const uint8*>(string), size);
	}


	// Blocks until the whole buffer is filled or an error occurs.
	ResultStatus ReadExact(uint8 *buffer, uint32 size) {
		uint32 received = 0;
		while (received < size) {
			Result<uint32> result = ReadByteArray(buffer + received, size - received);
			if (result.IsErr()) {
				return result.Error();
			}
			received += result.Value();
		}

		return ResultStatus::ok;
	}


protected:
	virtual ResultStatus Initialization() = 0;
};
