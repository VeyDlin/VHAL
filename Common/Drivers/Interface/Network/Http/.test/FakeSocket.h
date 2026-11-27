#pragma once
#include <Adapter/SocketAdapter.h>
#include <Utilities/Result/Result.h>
#include <cstring>
#include <functional>
#include <string>
#include <vector>


// Scripted SocketAdapter. Reads return the bytes of one slice at a time, so each slice behaves like one packet.
// A slice equal to CloseMarker makes one read return connectionClosed, then the script continues: that is a server
// that closed an idle keep-alive connection.
class FakeSocket : public SocketAdapter {
public:
	static inline const char *CloseMarker = "\x01" "CLOSE";

	std::vector<std::string> slices;
	std::string written;
	int openCount = 0;
	std::string lastHost;
	uint16 lastPort = 0;
	// What a read returns once the slices run out. connectionClosed is the normal end of a close-delimited body.
	ResultStatus endStatus = ResultStatus::connectionClosed;
	// Runs before every read, so a test can defragment the heap while the client is mid-parse.
	std::function<void()> beforeRead;
	// Connect timeout in Parameters when Open() was called, and the I/O timeout each read ran with, in order.
	uint32 connectTimeoutAtOpen = 0;
	std::vector<uint32> readTimeouts;

private:
	uint32 ioTimeoutMs = 10000;
	bool open = false;
	std::size_t nextSlice = 0;
	std::size_t offsetInSlice = 0;

public:
	ResultStatus Open(const char *host, uint16 port) override {
		open = true;
		openCount++;
		lastHost = host;
		lastPort = port;
		connectTimeoutAtOpen = parameters.connectTimeoutMs;
		return ResultStatus::ok;
	}


	ResultStatus Close() override {
		open = false;
		return ResultStatus::ok;
	}


	bool IsOpen() const override {
		return open;
	}


	Result<Ipv4Address> Resolve(const char *host) override {
		(void)host;
		return ResultStatus::notFound;
	}


	ResultStatus WriteByteArray(const uint8 *buffer, uint32 size) override {
		written.append(reinterpret_cast<const char *>(buffer), size);
		return ResultStatus::ok;
	}


	void SetIoTimeout(uint32 val) override {
		SocketAdapter::SetIoTimeout(val);
		ioTimeoutMs = val;
	}


	Result<uint32> ReadByteArray(uint8 *buffer, uint32 size) override {
		readTimeouts.push_back(ioTimeoutMs);
		if (beforeRead) {
			beforeRead();
		}

		while (nextSlice < slices.size() && offsetInSlice >= slices[nextSlice].size()) {
			nextSlice++;
			offsetInSlice = 0;
		}

		if (nextSlice >= slices.size()) {
			return endStatus;
		}

		if (slices[nextSlice] == CloseMarker) {
			nextSlice++;
			offsetInSlice = 0;
			return ResultStatus::connectionClosed;
		}

		const std::string& slice = slices[nextSlice];
		uint32 available = static_cast<uint32>(slice.size() - offsetInSlice);
		uint32 count = (size < available) ? size : available;
		std::memcpy(buffer, slice.data() + offsetInSlice, count);
		offsetInSlice += count;
		return count;
	}


protected:
	ResultStatus Initialization() override {
		return ResultStatus::ok;
	}
};
