#pragma once
#include <Adapter/SocketAdapter.h>
#include <Utilities/Crypto/SHA/SHA1.h>
#include <Utilities/Encoding/Base/Base64.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>


// SocketAdapter fake for byte-exact tests. Reads come from a script; writes are recorded. Time is simulated: a read
// that times out advances nowMs by the I/O timeout the client set (none when that is 0), so handshake, close and
// poll deadlines pass without sleeping.
class ScriptedSocket : public SocketAdapter {
public:
	static constexpr uint32 kSocketTimeoutMs = 1000;

	struct Step {
		std::vector<uint8> bytes;
		ResultStatus status = ResultStatus::ok;
	};


	std::deque<Step> script;
	std::vector<uint8> written;
	uint32 nowMs = 0;
	bool opened = false;
	int openCount = 0;
	int closeCount = 0;
	std::string lastHost;
	uint16 lastPort = 0;
	int writeCalls = 0;
	int failWriteCall = -1;
	// Connect timeout in Parameters when Open() was called, and the I/O timeout each read ran with, in order.
	uint32 connectTimeoutAtOpen = 0;
	uint32 ioTimeoutMs = kSocketTimeoutMs;
	std::vector<uint32> readTimeouts;

	// When set, the first request (ending in CRLF CRLF) is answered by pushing head + tail to the front of the script.
	// "{ACCEPT}" in head is replaced by the accept key computed from the request's Sec-WebSocket-Key.
	bool answerHandshake = false;
	std::string handshakeHead;
	std::vector<uint8> handshakeTail;
	size_t requestEnd = 0;


	void Queue(const std::vector<uint8>& bytes) {
		Step step;
		step.bytes = bytes;
		script.push_back(step);
	}


	void QueueText(const std::string& text) {
		Queue(std::vector<uint8>(text.begin(), text.end()));
	}


	void QueueStatus(ResultStatus status) {
		Step step;
		step.status = status;
		script.push_back(step);
	}


	ResultStatus Open(const char* host, uint16 hostPort) override {
		opened = true;
		openCount++;
		lastHost = host;
		lastPort = hostPort;
		connectTimeoutAtOpen = parameters.connectTimeoutMs;
		return ResultStatus::ok;
	}


	void SetIoTimeout(uint32 val) override {
		SocketAdapter::SetIoTimeout(val);
		ioTimeoutMs = val;
	}


	ResultStatus Close() override {
		if (opened) {
			closeCount++;
		}

		opened = false;
		return ResultStatus::ok;
	}


	bool IsOpen() const override {
		return opened;
	}


	Result<Ipv4Address> Resolve(const char* host) override {
		(void)host;
		return ResultStatus::notFound;
	}


	ResultStatus WriteByteArray(const uint8* buffer, uint32 size) override {
		writeCalls++;
		if (writeCalls == failWriteCall) {
			return ResultStatus::writeError;
		}

		written.insert(written.end(), buffer, buffer + size);

		if (answerHandshake && requestEnd == 0) {
			std::string text(written.begin(), written.end());
			size_t end = text.find("\r\n\r\n");
			if (end != std::string::npos) {
				requestEnd = end + 4;
				AnswerHandshake(text);
			}
		}

		return ResultStatus::ok;
	}


	Result<uint32> ReadByteArray(uint8* buffer, uint32 size) override {
		readTimeouts.push_back(ioTimeoutMs);
		while (!script.empty() && script.front().status == ResultStatus::ok && script.front().bytes.empty()) {
			script.pop_front();
		}

		if (script.empty()) {
			nowMs += ioTimeoutMs;
			return ResultStatus::timeout;
		}

		Step& step = script.front();
		if (step.status != ResultStatus::ok) {
			ResultStatus status = step.status;
			script.pop_front();
			if (status == ResultStatus::timeout) {
				nowMs += ioTimeoutMs;
			}

			return status;
		}

		uint32 count = std::min(size, static_cast<uint32>(step.bytes.size()));
		std::copy(step.bytes.begin(), step.bytes.begin() + count, buffer);
		step.bytes.erase(step.bytes.begin(), step.bytes.begin() + count);
		if (step.bytes.empty()) {
			script.pop_front();
		}

		return count;
	}


protected:
	ResultStatus Initialization() override {
		return ResultStatus::ok;
	}


private:
	void AnswerHandshake(const std::string& request) {
		std::string keyLine = "Sec-WebSocket-Key: ";
		size_t keyStart = request.find(keyLine);
		std::string key = request.substr(keyStart + keyLine.size(), 24);

		SHA1Hasher hasher;
		hasher.Update(std::string_view(key));
		hasher.Update(std::string_view("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
		std::array<uint8, 20> digest = hasher.Finalize();
		char accept[32] = {};
		size_t length = Base64::Encode(std::span<char>(accept, sizeof(accept)), std::span<const uint8>(digest));

		std::string head = handshakeHead;
		size_t placeholder = head.find("{ACCEPT}");
		if (placeholder != std::string::npos) {
			head.replace(placeholder, 8, std::string(accept, length));
		}

		std::vector<uint8> bytes(head.begin(), head.end());
		bytes.insert(bytes.end(), handshakeTail.begin(), handshakeTail.end());

		Step step;
		step.bytes = bytes;
		script.push_front(step);
	}
};


// Deterministic random source: call n returns 0x9E3779B9 * (n + 1). Calls 0..3 make the handshake key, and frame k
// uses call 4 + k for its mask. MaskFor(k) gives that mask.
struct TestRandom {
	uint32 calls = 0;


	uint32 Next() {
		uint32 value = 0x9E3779B9u * (calls + 1);
		calls++;
		return value;
	}


	static uint32 MaskFor(uint32 frameIndex) {
		return 0x9E3779B9u * (frameIndex + 5);
	}
};


inline void AppendLength(std::vector<uint8>& out, uint64 length, bool masked) {
	uint8 maskBit = masked ? 0x80 : 0x00;
	if (length < 126) {
		out.push_back(static_cast<uint8>(maskBit | length));
	} else if (length <= 0xFFFF) {
		out.push_back(static_cast<uint8>(maskBit | 126));
		out.push_back(static_cast<uint8>(length >> 8));
		out.push_back(static_cast<uint8>(length));
	} else {
		out.push_back(static_cast<uint8>(maskBit | 127));
		for (int shift = 56; shift >= 0; shift -= 8) {
			out.push_back(static_cast<uint8>(length >> shift));
		}
	}
}


// Unmasked frame as a server sends it, with minimal length encoding.
inline std::vector<uint8> ServerFrame(bool fin, uint8 opcode, const std::vector<uint8>& payload) {
	std::vector<uint8> out;
	out.push_back(static_cast<uint8>((fin ? 0x80 : 0x00) | opcode));
	AppendLength(out, payload.size(), false);
	out.insert(out.end(), payload.begin(), payload.end());
	return out;
}


// Masked frame as a client must send it.
inline std::vector<uint8> ClientFrame(bool fin, uint8 opcode, uint32 mask, const std::vector<uint8>& payload) {
	std::vector<uint8> out;
	out.push_back(static_cast<uint8>((fin ? 0x80 : 0x00) | opcode));
	AppendLength(out, payload.size(), true);
	std::vector<uint8> key = { static_cast<uint8>(mask >> 24), static_cast<uint8>(mask >> 16),
		static_cast<uint8>(mask >> 8), static_cast<uint8>(mask) };
	out.insert(out.end(), key.begin(), key.end());
	for (size_t i = 0; i < payload.size(); i++) {
		out.push_back(payload[i] ^ key[i & 3]);
	}

	return out;
}


inline std::vector<uint8> Bytes(const std::string& text) {
	return std::vector<uint8>(text.begin(), text.end());
}


inline std::vector<uint8> Concat(std::vector<uint8> first, const std::vector<uint8>& second) {
	first.insert(first.end(), second.begin(), second.end());
	return first;
}
