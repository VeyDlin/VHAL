#pragma once
#include <VHAL.h>
#include <Adapter/SocketAdapter.h>
#include <Utilities/Crypto/SHA/SHA1.h>
#include <Utilities/Encoding/Base/Base64.h>
#include <Utilities/VSTD/memory/IAllocator.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <span>
#include <string_view>


// WebSocket client (RFC 6455, client side) on top of a SocketAdapter. ws:// uses a Tcp socket, wss:// a Tls socket.
// Not thread-safe: one task drives Connect, the Send calls, Poll and Close. See .wiki for the full contract.
class WebSocketClient {
public:
	struct Header {
		std::string_view name;
		std::string_view value;
	};


	struct ConnectOptions {
		// Extra request headers, for example { "Authorization", "Bearer ..." }. Host, Upgrade, Connection and the
		// Sec-WebSocket-* headers belong to the client and are rejected here.
		std::span<const Header> headers{};
		// Offered in Sec-WebSocket-Protocol, most preferred first. The server must pick one of them, or none.
		std::span<const std::string_view> subprotocols{};
		// Required. Returns 32 random bits per call. The handshake key and every frame mask come from it.
		std::function<uint32()> randomSource;
		// Required. Milliseconds from a monotonic clock, for example esp_timer_get_time() / 1000.
		std::function<uint32()> clockMs;
		uint32 handshakeTimeoutMs = 10000;
		// How long Close() waits for the peer's close frame before the socket is closed anyway.
		uint32 closeTimeoutMs = 5000;
		// Largest message accepted or sent, in payload bytes. A larger incoming message closes the connection with 1009.
		uint32 maxMessageSize = 65536;
		// Outgoing messages are split into frames of at most this payload size. 0 sends one frame per message.
		uint32 fragmentSize = 0;
		// Bytes of a rejected (non-101) answer's body kept for GetRejectBody(). Clamped to 512.
		uint32 maxRejectBodyBytes = 256;
	};


	// The views passed to these callbacks are valid only while the callback runs.
	std::function<void(std::string_view text)> onText;
	std::function<void(std::span<const uint8> data)> onBinary;
	std::function<void(uint16 code, std::string_view reason)> onClose;


private:
	enum class State : uint8 {
		closed,
		open,
		closing
	};


	enum class FrameStage : uint8 {
		header,
		extendedLength,
		payload
	};


	// Incremental UTF-8 check (RFC 3629 ranges), so invalid text is caught early and across fragments.
	struct Utf8Checker {
		uint8 needed = 0;
		uint8 lowest = 0x80;
		uint8 highest = 0xBF;

		bool Feed(uint8 byte) {
			if (needed == 0) {
				if (byte < 0x80) {
					return true;
				}

				if (byte >= 0xC2 && byte <= 0xDF) {
					needed = 1;
				} else if (byte == 0xE0) {
					needed = 2;
					lowest = 0xA0;
				} else if ((byte >= 0xE1 && byte <= 0xEC) || byte == 0xEE || byte == 0xEF) {
					needed = 2;
				} else if (byte == 0xED) {
					needed = 2;
					highest = 0x9F;
				} else if (byte == 0xF0) {
					needed = 3;
					lowest = 0x90;
				} else if (byte >= 0xF1 && byte <= 0xF3) {
					needed = 3;
				} else if (byte == 0xF4) {
					needed = 3;
					highest = 0x8F;
				} else {
					return false;
				}

				return true;
			}

			if (byte < lowest || byte > highest) {
				return false;
			}

			needed--;
			lowest = 0x80;
			highest = 0xBF;
			return true;
		}


		bool Complete() const {
			return needed == 0;
		}
	};


	static constexpr size_t kRxBufferSize = 1024;
	static constexpr size_t kTxBufferSize = 256;
	static constexpr size_t kMaxHostLength = 127;
	static constexpr size_t kMaxPathLength = 255;
	static constexpr size_t kMaxSubprotocolLength = 64;
	static constexpr size_t kMaxRejectBody = 512;
	static constexpr size_t kMaxControlPayload = 125;
	static constexpr size_t kMaxCloseReason = 123;
	static constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

	SocketAdapter& adapter;
	VSTD::IAllocator& memory;

	std::function<uint32()> randomSource;
	std::function<uint32()> clockMs;
	uint32 handshakeTimeoutMs = 10000;
	uint32 closeTimeoutMs = 5000;
	uint32 maxMessageSize = 65536;
	uint32 fragmentSize = 0;
	uint32 maxRejectBody = 256;

	State state = State::closed;
	ResultStatus lastStatus = ResultStatus::ok;
	uint16 handshakeStatus = 0;
	uint16 closeCode = 0;
	bool peerCloseReceived = false;
	size_t closeReasonLength = 0;
	std::array<char, kMaxCloseReason> closeReason{};
	size_t subprotocolLength = 0;
	std::array<char, kMaxSubprotocolLength> subprotocol{};
	size_t rejectBodyLength = 0;
	std::array<char, kMaxRejectBody> rejectBody{};

	std::array<char, kMaxHostLength + 1> hostName{};
	std::array<char, kMaxPathLength + 1> path{};
	size_t pathLength = 0;
	uint16 port = 0;
	bool secure = false;
	std::array<char, 25> keyText{};
	std::array<char, 29> acceptText{};

	std::array<uint8, kRxBufferSize> rxBuffer{};
	size_t rxPos = 0;
	size_t rxLen = 0;
	std::array<uint8, kTxBufferSize> txBuffer{};
	size_t txLength = 0;
	ResultStatus txStatus = ResultStatus::ok;

	FrameStage frameStage = FrameStage::header;
	std::array<uint8, 10> frameHead{};
	size_t frameHeadFill = 0;
	size_t frameHeadNeeded = 2;
	bool frameFin = false;
	uint8 frameOpcode = 0;
	uint64 frameLength = 0;
	uint64 frameOffset = 0;
	std::array<uint8, kMaxControlPayload> controlPayload{};

	bool messageInProgress = false;
	uint8 messageOpcode = 0;
	VSTD::Handle messageHandle;
	size_t messageLength = 0;
	Utf8Checker utf8;


public:
	WebSocketClient(SocketAdapter& adapterRef, VSTD::IAllocator& memoryRef) : adapter(adapterRef), memory(memoryRef) {
	}


	WebSocketClient(const WebSocketClient&) = delete;
	WebSocketClient& operator=(const WebSocketClient&) = delete;


	~WebSocketClient() {
		if (state != State::closed) {
			adapter.Close();
		}

		if (messageHandle.IsValid()) {
			memory.Deallocate(messageHandle);
		}
	}


	// Parses url (ws:// or wss://), performs the opening handshake and leaves the client open.
	// Returns: busy (already connected), invalidParameter (bad URL, header or options, or no random/clock source),
	// notSupported (IPv6 literal), noInit, notFound, noConnection, timeout (handshake), connectionClosed, readError,
	// writeError (socket), invalidData (malformed or oversized answer), protocolError (answer does not satisfy RFC 6455
	// 4.1), authenticationFailed (401), authorizationFailed (403). After a non-101 answer see GetHandshakeStatus() and
	// GetRejectBody().
	ResultStatus Connect(const char* url, const ConnectOptions& options) {
		if (state != State::closed) {
			return ResultStatus::busy;
		}

		if (!options.randomSource || !options.clockMs) {
			return ResultStatus::invalidParameter;
		}

		ResultStatus status = ParseUrl(url);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = ValidateOptions(options);
		if (status != ResultStatus::ok) {
			return status;
		}

		randomSource = options.randomSource;
		clockMs = options.clockMs;
		handshakeTimeoutMs = options.handshakeTimeoutMs;
		closeTimeoutMs = options.closeTimeoutMs;
		maxMessageSize = options.maxMessageSize;
		fragmentSize = options.fragmentSize;
		maxRejectBody = std::min(options.maxRejectBodyBytes, static_cast<uint32>(kMaxRejectBody));

		closeCode = 0;
		closeReasonLength = 0;
		handshakeStatus = 0;
		rejectBodyLength = 0;
		subprotocolLength = 0;
		peerCloseReceived = false;
		lastStatus = ResultStatus::ok;

		SocketAdapter::Parameters parameters = adapter.GetParameters();
		SocketAdapter::Protocol wanted = secure ? SocketAdapter::Protocol::Tls : SocketAdapter::Protocol::Tcp;
		if (parameters.protocol != wanted) {
			parameters.protocol = wanted;
			status = adapter.SetParameters(parameters);
			if (status != ResultStatus::ok) {
				return status;
			}
		}

		status = adapter.Open(hostName.data(), port);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = RunHandshake(options);
		if (status != ResultStatus::ok) {
			adapter.Close();
			return status;
		}

		state = State::open;
		frameStage = FrameStage::header;
		frameHeadFill = 0;
		frameHeadNeeded = 2;
		return ResultStatus::ok;
	}


	// Sends a text message. The text must be valid UTF-8 (invalidParameter otherwise).
	ResultStatus SendText(std::string_view text) {
		if (state != State::open) {
			return ResultStatus::noConnection;
		}

		if (!IsValidUtf8(text)) {
			return ResultStatus::invalidParameter;
		}

		return SendMessage(0x1, reinterpret_cast<const uint8*>(text.data()), text.size());
	}


	ResultStatus SendBinary(std::span<const uint8> data) {
		if (state != State::open) {
			return ResultStatus::noConnection;
		}

		return SendMessage(0x2, data.data(), data.size());
	}


	// Sends a ping with at most 125 bytes of payload. The peer answers with a pong, handled by Poll().
	ResultStatus SendPing(std::span<const uint8> payload) {
		if (state != State::open) {
			return ResultStatus::noConnection;
		}

		if (payload.size() > kMaxControlPayload) {
			return ResultStatus::invalidParameter;
		}

		return SendFrame(0x9, true, payload.data(), payload.size());
	}


	// Sends a close frame and waits up to closeTimeoutMs for the peer's close frame, dispatching any data that
	// arrives meanwhile. code 0 sends an empty close payload (no status code). Returns ok when the peer answered,
	// timeout when it did not, and connectionClosed or readError when the socket ended first. The socket is closed
	// in every case, and onClose runs once. Calling it on a closed client returns ok.
	ResultStatus Close(uint16 code = 1000, std::string_view reason = std::string_view()) {
		if (state != State::open) {
			return ResultStatus::ok;
		}

		if (code != 0) {
			if (!IsSendableCloseCode(code) || reason.size() > kMaxCloseReason || !IsValidUtf8(reason)) {
				return ResultStatus::invalidParameter;
			}
		} else if (!reason.empty()) {
			return ResultStatus::invalidParameter;
		}

		std::array<uint8, kMaxControlPayload> payload{};
		size_t payloadLength = 0;
		if (code != 0) {
			payload[0] = static_cast<uint8>(code >> 8);
			payload[1] = static_cast<uint8>(code);
			if (!reason.empty()) {
				std::memcpy(payload.data() + 2, reason.data(), reason.size());
			}

			payloadLength = 2 + reason.size();
		}

		ResultStatus status = SendFrame(0x8, true, payload.data(), payloadLength);
		if (status != ResultStatus::ok) {
			return status;
		}

		state = State::closing;
		uint32 startMs = clockMs();
		while (state == State::closing) {
			ReceiveOnce();
			if (state == State::closing && Expired(startMs, closeTimeoutMs)) {
				Finish(1006, std::string_view(), ResultStatus::timeout);
			}
		}

		if (peerCloseReceived) {
			return ResultStatus::ok;
		}

		return lastStatus;
	}


	// Reads what is available and dispatches complete messages to the callbacks. Returns ok when something was
	// processed, timeout when nothing arrived, and connectionClosed when the connection ended (see GetCloseCode()).
	// A protocol error returns that error after the close frame was sent. The first read blocks for at most the
	// SocketAdapter timeout (SetTimeout before Open); the loop then keeps reading until timeoutMs has passed.
	// Poll() with a timeoutMs shorter than the socket timeout can therefore take up to the socket timeout.
	ResultStatus Poll(uint32 timeoutMs) {
		if (state == State::closed) {
			return ResultStatus::noConnection;
		}

		uint32 startMs = clockMs();
		bool progressed = false;
		while (true) {
			ResultStatus status = ReceiveOnce();
			if (status == ResultStatus::timeout) {
				return progressed ? ResultStatus::ok : ResultStatus::timeout;
			}

			if (status != ResultStatus::ok) {
				return status;
			}

			progressed = true;
			if (Expired(startMs, timeoutMs)) {
				return ResultStatus::ok;
			}
		}
	}


	bool IsOpen() const {
		return state == State::open;
	}


	// 0 until the connection closes. After a close: the code the peer sent, or the code this side chose on a
	// protocol error, or 1005 (no status code received), or 1006 (no close frame: the socket ended or timed out).
	uint16 GetCloseCode() const {
		return closeCode;
	}


	std::string_view GetCloseReason() const {
		return std::string_view(closeReason.data(), closeReasonLength);
	}


	// HTTP status of the handshake answer, 0 before any answer. 101 when the connection opened.
	uint16 GetHandshakeStatus() const {
		return handshakeStatus;
	}


	// Up to maxRejectBodyBytes of the body of a non-101 answer. Chunked bodies are de-chunked.
	std::string_view GetRejectBody() const {
		return std::string_view(rejectBody.data(), rejectBodyLength);
	}


	// The subprotocol the server chose, empty when none.
	std::string_view GetSubprotocol() const {
		return std::string_view(subprotocol.data(), subprotocolLength);
	}


private:
	static bool IsSendableCloseCode(uint16 code) {
		return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999);
	}


	static bool IsKnownOpcode(uint8 opcode) {
		return opcode == 0x0 || opcode == 0x1 || opcode == 0x2 || opcode == 0x8 || opcode == 0x9 || opcode == 0xA;
	}


	static bool IsControlOpcode(uint8 opcode) {
		return opcode >= 0x8;
	}


	static bool IsValidUtf8(std::string_view text) {
		Utf8Checker checker;
		for (char c : text) {
			if (!checker.Feed(static_cast<uint8>(c))) {
				return false;
			}
		}

		return checker.Complete();
	}


	static char LowerAscii(char c) {
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
	}


	static bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
		if (a.size() != b.size()) {
			return false;
		}

		for (size_t i = 0; i < a.size(); i++) {
			if (LowerAscii(a[i]) != LowerAscii(b[i])) {
				return false;
			}
		}

		return true;
	}


	static std::string_view Trim(std::string_view text) {
		size_t first = 0;
		size_t last = text.size();
		while (first < last && (text[first] == ' ' || text[first] == '\t')) {
			first++;
		}

		while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t')) {
			last--;
		}

		return text.substr(first, last - first);
	}


	// True when one of the comma-separated items of list equals token (case-insensitive).
	static bool ContainsToken(std::string_view list, std::string_view token) {
		while (true) {
			size_t comma = list.find(',');
			std::string_view item = Trim(list.substr(0, comma));
			if (EqualsIgnoreCase(item, token)) {
				return true;
			}

			if (comma == std::string_view::npos) {
				return false;
			}

			list.remove_prefix(comma + 1);
		}
	}


	static bool IsVisibleAscii(char c) {
		return static_cast<uint8>(c) > 0x20 && static_cast<uint8>(c) < 0x7F;
	}


	// RFC 7230 token characters, used for header names and subprotocol names.
	static bool IsToken(std::string_view text) {
		if (text.empty()) {
			return false;
		}

		for (char c : text) {
			bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
			bool mark = std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
			if (!alnum && !mark) {
				return false;
			}
		}

		return true;
	}


	// Header values: visible characters, spaces and tabs. No CR, LF, NUL or other control characters.
	static bool IsValidHeaderValue(std::string_view text) {
		for (char c : text) {
			uint8 byte = static_cast<uint8>(c);
			if (byte == '\t') {
				continue;
			}

			if (byte < 0x20 || byte == 0x7F) {
				return false;
			}
		}

		return true;
	}


	static bool IsReservedHeader(std::string_view name) {
		return EqualsIgnoreCase(name, "Host") || EqualsIgnoreCase(name, "Upgrade") ||
			EqualsIgnoreCase(name, "Connection") || EqualsIgnoreCase(name, "Sec-WebSocket-Key") ||
			EqualsIgnoreCase(name, "Sec-WebSocket-Version") || EqualsIgnoreCase(name, "Sec-WebSocket-Protocol") ||
			EqualsIgnoreCase(name, "Sec-WebSocket-Extensions") || EqualsIgnoreCase(name, "Sec-WebSocket-Accept");
	}


	// Takes the next "name: value" line off block (which holds CRLF-terminated lines). False when block is empty.
	static bool NextHeaderLine(std::string_view& block, std::string_view& name, std::string_view& value) {
		size_t lineEnd = block.find("\r\n");
		if (lineEnd == std::string_view::npos) {
			return false;
		}

		std::string_view line = block.substr(0, lineEnd);
		block.remove_prefix(lineEnd + 2);

		size_t colon = line.find(':');
		name = Trim(line.substr(0, colon));
		value = Trim(line.substr(colon + 1));
		return true;
	}


	// Every header line needs a colon and must not start with whitespace (obsolete line folding is rejected).
	static bool HeaderBlockWellFormed(std::string_view block) {
		std::string_view rest = block;
		while (!rest.empty()) {
			size_t lineEnd = rest.find("\r\n");
			if (lineEnd == std::string_view::npos) {
				return false;
			}

			std::string_view line = rest.substr(0, lineEnd);
			if (line.empty() || line.find(':') == std::string_view::npos || line[0] == ' ' || line[0] == '\t') {
				return false;
			}

			rest.remove_prefix(lineEnd + 2);
		}

		return true;
	}


	static bool ParseDecimal(std::string_view text, uint64& value) {
		if (text.empty() || text.size() > 18) {
			return false;
		}

		value = 0;
		for (char c : text) {
			if (c < '0' || c > '9') {
				return false;
			}

			value = value * 10 + static_cast<uint64>(c - '0');
		}

		return true;
	}


	static int HexValue(char c) {
		if (c >= '0' && c <= '9') {
			return c - '0';
		}

		if (c >= 'a' && c <= 'f') {
			return c - 'a' + 10;
		}

		if (c >= 'A' && c <= 'F') {
			return c - 'A' + 10;
		}

		return -1;
	}


	bool Expired(uint32 startMs, uint32 timeoutMs) {
		return clockMs() - startMs >= timeoutMs;
	}


	ResultStatus ParseUrl(const char* url) {
		if (url == nullptr) {
			return ResultStatus::invalidParameter;
		}

		std::string_view text(url);
		if (text.size() >= 6 && EqualsIgnoreCase(text.substr(0, 6), "wss://")) {
			secure = true;
			text.remove_prefix(6);
		} else if (text.size() >= 5 && EqualsIgnoreCase(text.substr(0, 5), "ws://")) {
			secure = false;
			text.remove_prefix(5);
		} else {
			return ResultStatus::invalidParameter;
		}

		// Fragments are not part of a WebSocket resource name (RFC 6455 3).
		if (text.find('#') != std::string_view::npos) {
			return ResultStatus::invalidParameter;
		}

		size_t pathStart = text.find_first_of("/?");
		std::string_view authority = (pathStart == std::string_view::npos) ? text : text.substr(0, pathStart);
		std::string_view remainder = (pathStart == std::string_view::npos) ? std::string_view() : text.substr(pathStart);

		if (authority.empty()) {
			return ResultStatus::invalidParameter;
		}

		if (authority.front() == '[') {
			return ResultStatus::notSupported;
		}

		if (authority.find('@') != std::string_view::npos) {
			return ResultStatus::invalidParameter;
		}

		uint16 portValue = secure ? 443 : 80;
		std::string_view hostPart = authority;
		size_t colon = authority.find(':');
		if (colon != std::string_view::npos) {
			hostPart = authority.substr(0, colon);
			uint64 parsed = 0;
			if (!ParseDecimal(authority.substr(colon + 1), parsed) || parsed == 0 || parsed > 65535) {
				return ResultStatus::invalidParameter;
			}

			portValue = static_cast<uint16>(parsed);
		}

		if (hostPart.empty() || hostPart.size() > kMaxHostLength) {
			return ResultStatus::invalidParameter;
		}

		for (char c : hostPart) {
			if (!IsVisibleAscii(c)) {
				return ResultStatus::invalidParameter;
			}
		}

		// The resource is "/" + query when the URL has no path, and the path and query as written otherwise.
		std::string_view resource = remainder;
		pathLength = 0;
		if (resource.empty() || resource.front() == '?') {
			path[pathLength++] = '/';
		}

		if (pathLength + resource.size() > kMaxPathLength) {
			return ResultStatus::invalidParameter;
		}

		for (char c : resource) {
			if (!IsVisibleAscii(c)) {
				return ResultStatus::invalidParameter;
			}
		}

		if (!resource.empty()) {
			std::memcpy(path.data() + pathLength, resource.data(), resource.size());
		}

		pathLength += resource.size();
		path[pathLength] = '\0';

		std::memcpy(hostName.data(), hostPart.data(), hostPart.size());
		hostName[hostPart.size()] = '\0';

		port = portValue;
		return ResultStatus::ok;
	}


	ResultStatus ValidateOptions(const ConnectOptions& options) {
		for (const Header& header : options.headers) {
			if (!IsToken(header.name) || IsReservedHeader(header.name) || !IsValidHeaderValue(header.value)) {
				return ResultStatus::invalidParameter;
			}
		}

		for (std::string_view protocolName : options.subprotocols) {
			if (!IsToken(protocolName)) {
				return ResultStatus::invalidParameter;
			}
		}

		return ResultStatus::ok;
	}


	ResultStatus RunHandshake(const ConnectOptions& options) {
		ResultStatus status = MakeKeyAndAccept();
		if (status != ResultStatus::ok) {
			return status;
		}

		uint32 startMs = clockMs();

		status = WriteRequest(options);
		if (status != ResultStatus::ok) {
			return status;
		}

		size_t headerEnd = 0;
		status = ReadResponseHead(startMs, headerEnd);
		if (status != ResultStatus::ok) {
			return status;
		}

		std::string_view response(reinterpret_cast<const char*>(rxBuffer.data()), headerEnd + 4);
		size_t statusLineEnd = response.find("\r\n");
		std::string_view statusLine = response.substr(0, statusLineEnd);
		if (statusLine.size() < 12 || statusLine.substr(0, 9) != "HTTP/1.1 ") {
			return ResultStatus::invalidData;
		}

		uint64 code = 0;
		bool codeEndsWell = statusLine.size() == 12 || statusLine[12] == ' ';
		if (!ParseDecimal(statusLine.substr(9, 3), code) || !codeEndsWell) {
			return ResultStatus::invalidData;
		}

		handshakeStatus = static_cast<uint16>(code);

		std::string_view headerBlock = response.substr(statusLineEnd + 2, headerEnd + 2 - (statusLineEnd + 2));
		if (!HeaderBlockWellFormed(headerBlock)) {
			return ResultStatus::invalidData;
		}

		if (code != 101) {
			ReadRejectBody(startMs, headerEnd, headerBlock);
			adapter.Close();
			if (code == 401) {
				return ResultStatus::authenticationFailed;
			}

			if (code == 403) {
				return ResultStatus::authorizationFailed;
			}

			return ResultStatus::protocolError;
		}

		status = ValidateUpgrade(headerBlock, options);
		if (status != ResultStatus::ok) {
			return status;
		}

		rxPos = headerEnd + 4;
		return ResultStatus::ok;
	}


	ResultStatus MakeKeyAndAccept() {
		std::array<uint8, 16> nonce{};
		for (size_t i = 0; i < nonce.size(); i += 4) {
			uint32 bits = randomSource();
			nonce[i] = static_cast<uint8>(bits >> 24);
			nonce[i + 1] = static_cast<uint8>(bits >> 16);
			nonce[i + 2] = static_cast<uint8>(bits >> 8);
			nonce[i + 3] = static_cast<uint8>(bits);
		}

		size_t keyLength = Base64::Encode(std::span<char>(keyText), std::span<const uint8>(nonce));
		if (keyLength != 24) {
			return ResultStatus::unknownError;
		}

		SHA1Hasher hasher;
		hasher.Update(std::string_view(keyText.data(), keyLength));
		hasher.Update(kGuid);
		std::array<uint8, 20> digest = hasher.Finalize();

		size_t acceptLength = Base64::Encode(std::span<char>(acceptText), std::span<const uint8>(digest));
		if (acceptLength != 28) {
			return ResultStatus::unknownError;
		}

		return ResultStatus::ok;
	}


	ResultStatus WriteRequest(const ConnectOptions& options) {
		txLength = 0;
		txStatus = ResultStatus::ok;
		TxAppend("GET ");
		TxAppend(std::string_view(path.data(), pathLength));
		TxAppend(" HTTP/1.1\r\nHost: ");
		TxAppend(std::string_view(hostName.data()));

		bool defaultPort = secure ? (port == 443) : (port == 80);
		if (!defaultPort) {
			char digits[6] = {};
			size_t digitCount = 0;
			uint16 remaining = port;
			do {
				digits[digitCount++] = static_cast<char>('0' + remaining % 10);
				remaining = static_cast<uint16>(remaining / 10);
			} while (remaining != 0);

			TxAppend(":");
			while (digitCount > 0) {
				digitCount--;
				TxAppend(std::string_view(&digits[digitCount], 1));
			}
		}

		TxAppend("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ");
		TxAppend(std::string_view(keyText.data(), 24));
		TxAppend("\r\nSec-WebSocket-Version: 13\r\n");

		if (!options.subprotocols.empty()) {
			TxAppend("Sec-WebSocket-Protocol: ");
			bool first = true;
			for (std::string_view protocolName : options.subprotocols) {
				if (!first) {
					TxAppend(", ");
				}

				TxAppend(protocolName);
				first = false;
			}

			TxAppend("\r\n");
		}

		for (const Header& header : options.headers) {
			TxAppend(header.name);
			TxAppend(": ");
			TxAppend(header.value);
			TxAppend("\r\n");
		}

		TxAppend("\r\n");
		return FlushTx();
	}


	// Reads until the blank line that ends the answer's header block. headerEnd is the index of that CRLF CRLF.
	ResultStatus ReadResponseHead(uint32 startMs, size_t& headerEnd) {
		rxPos = 0;
		rxLen = 0;

		while (true) {
			std::string_view received(reinterpret_cast<const char*>(rxBuffer.data()), rxLen);
			size_t found = received.find("\r\n\r\n");
			if (found != std::string_view::npos) {
				headerEnd = found;
				return ResultStatus::ok;
			}

			ResultStatus status = ReadHandshakeBytes(startMs);
			if (status != ResultStatus::ok) {
				return status;
			}
		}
	}


	// Appends bytes to rxBuffer. Returns invalidData when the buffer is full, timeout after the handshake deadline.
	ResultStatus ReadHandshakeBytes(uint32 startMs) {
		while (true) {
			if (rxLen >= rxBuffer.size()) {
				return ResultStatus::invalidData;
			}

			Result<uint32> result = adapter.ReadByteArray(rxBuffer.data() + rxLen, static_cast<uint32>(rxBuffer.size() - rxLen));
			if (result.IsOk()) {
				rxLen += result.Value();
				return ResultStatus::ok;
			}

			if (result.Error() != ResultStatus::timeout) {
				return result.Error();
			}

			if (Expired(startMs, handshakeTimeoutMs)) {
				return ResultStatus::timeout;
			}
		}
	}


	// Keeps the body of a rejected answer (up to maxRejectBody). Errors are ignored: the status is already known.
	void ReadRejectBody(uint32 startMs, size_t headerEnd, std::string_view headerBlock) {
		bool chunked = false;
		bool hasLength = false;
		uint64 length = 0;

		std::string_view rest = headerBlock;
		std::string_view name;
		std::string_view value;
		while (NextHeaderLine(rest, name, value)) {
			if (EqualsIgnoreCase(name, "Transfer-Encoding") && ContainsToken(value, "chunked")) {
				chunked = true;
			} else if (EqualsIgnoreCase(name, "Content-Length")) {
				hasLength = ParseDecimal(value, length);
			}
		}

		size_t bodyStart = headerEnd + 4;
		while (true) {
			bool complete = false;
			if (chunked) {
				complete = DecodeChunkedBody(bodyStart);
			} else if (hasLength) {
				complete = rxLen - bodyStart >= length;
			}

			if (complete || rxLen >= rxBuffer.size()) {
				break;
			}

			if (ReadHandshakeBytes(startMs) != ResultStatus::ok) {
				break;
			}
		}

		if (chunked) {
			DecodeChunkedBody(bodyStart);
			return;
		}

		rejectBodyLength = 0;

		size_t available = rxLen - bodyStart;
		size_t count = available;
		if (hasLength && length < available) {
			count = static_cast<size_t>(length);
		}

		count = std::min(count, static_cast<size_t>(maxRejectBody));
		std::memcpy(rejectBody.data(), rxBuffer.data() + bodyStart, count);
		rejectBodyLength = count;
	}


	// Rebuilds rejectBody from the chunks received so far. Returns true when the body is complete (zero-size chunk,
	// the limit, or malformed chunk framing, after which nothing more is worth reading).
	bool DecodeChunkedBody(size_t bodyStart) {
		std::string_view received(reinterpret_cast<const char*>(rxBuffer.data()), rxLen);
		size_t limit = static_cast<size_t>(maxRejectBody);
		size_t position = bodyStart;
		size_t written = 0;
		rejectBodyLength = 0;

		while (true) {
			size_t lineEnd = received.find("\r\n", position);
			if (lineEnd == std::string_view::npos) {
				rejectBodyLength = written;
				return false;
			}

			size_t chunkSize = 0;
			size_t digitCount = 0;
			for (size_t i = position; i < lineEnd; i++) {
				char c = static_cast<char>(rxBuffer[i]);
				if (c == ';') {
					break;
				}

				int digit = HexValue(c);
				if (digit < 0 || digitCount >= 8) {
					rejectBodyLength = written;
					return true;
				}

				chunkSize = chunkSize * 16 + static_cast<size_t>(digit);
				digitCount++;
			}

			if (digitCount == 0) {
				rejectBodyLength = written;
				return true;
			}

			if (chunkSize == 0) {
				rejectBodyLength = written;
				return true;
			}

			size_t dataStart = lineEnd + 2;
			size_t have = (rxLen > dataStart) ? rxLen - dataStart : 0;
			size_t count = std::min(std::min(chunkSize, have), limit - written);
			std::memcpy(rejectBody.data() + written, rxBuffer.data() + dataStart, count);
			written += count;

			if (written >= limit) {
				rejectBodyLength = written;
				return true;
			}

			if (have < chunkSize + 2) {
				rejectBodyLength = written;
				return false;
			}

			position = dataStart + chunkSize + 2;
		}
	}


	ResultStatus ValidateUpgrade(std::string_view headerBlock, const ConnectOptions& options) {
		bool upgrade = false;
		bool connection = false;
		bool extensions = false;
		size_t acceptCount = 0;
		std::string_view accept;
		size_t protocolCount = 0;
		std::string_view chosen;

		std::string_view rest = headerBlock;
		std::string_view name;
		std::string_view value;
		while (NextHeaderLine(rest, name, value)) {
			if (EqualsIgnoreCase(name, "Upgrade")) {
				upgrade = upgrade || ContainsToken(value, "websocket");
			} else if (EqualsIgnoreCase(name, "Connection")) {
				connection = connection || ContainsToken(value, "upgrade");
			} else if (EqualsIgnoreCase(name, "Sec-WebSocket-Accept")) {
				acceptCount++;
				accept = value;
			} else if (EqualsIgnoreCase(name, "Sec-WebSocket-Extensions")) {
				extensions = true;
			} else if (EqualsIgnoreCase(name, "Sec-WebSocket-Protocol")) {
				protocolCount++;
				chosen = value;
			}
		}

		if (!upgrade || !connection || acceptCount != 1 || accept != std::string_view(acceptText.data(), 28)) {
			return ResultStatus::protocolError;
		}

		// No extensions are offered, so the server must not accept one (RFC 6455 4.1).
		if (extensions || protocolCount > 1) {
			return ResultStatus::protocolError;
		}

		if (protocolCount == 1) {
			bool offered = false;
			for (std::string_view protocolName : options.subprotocols) {
				if (protocolName == chosen) {
					offered = true;
				}
			}

			if (!offered || chosen.size() > kMaxSubprotocolLength) {
				return ResultStatus::protocolError;
			}

			std::memcpy(subprotocol.data(), chosen.data(), chosen.size());
			subprotocolLength = chosen.size();
		}

		return ResultStatus::ok;
	}


	// Output buffer: frames and the handshake go through here, flushed to the socket when full. After the first
	// failed write txStatus stays set, so later flushes of the same message do not touch the socket.
	void TxAppend(std::string_view text) {
		for (char c : text) {
			if (txLength == txBuffer.size()) {
				FlushTx();
			}

			txBuffer[txLength++] = static_cast<uint8>(c);
		}
	}


	ResultStatus FlushTx() {
		if (txStatus != ResultStatus::ok) {
			txLength = 0;
			return txStatus;
		}

		if (txLength == 0) {
			return ResultStatus::ok;
		}

		txStatus = adapter.WriteByteArray(txBuffer.data(), static_cast<uint32>(txLength));
		txLength = 0;
		if (txStatus != ResultStatus::ok) {
			Finish(1006, std::string_view(), txStatus);
		}

		return txStatus;
	}


	// Data messages larger than fragmentSize are split. A write error drops the connection, because a
	// partly written frame leaves the stream unusable.
	ResultStatus SendMessage(uint8 opcode, const uint8* data, size_t length) {
		if (fragmentSize == 0 || length <= fragmentSize) {
			return SendFrame(opcode, true, data, length);
		}

		size_t offset = 0;
		bool first = true;
		while (offset < length) {
			size_t count = std::min(static_cast<size_t>(fragmentSize), length - offset);
			bool fin = offset + count == length;
			uint8 frameOpcodeValue = first ? opcode : 0x0;
			ResultStatus status = SendFrame(frameOpcodeValue, fin, data + offset, count);
			if (status != ResultStatus::ok) {
				return status;
			}

			first = false;
			offset += count;
		}

		return ResultStatus::ok;
	}


	// Client frames are masked with a fresh 4-byte key (RFC 6455 5.3).
	ResultStatus SendFrame(uint8 opcode, bool fin, const uint8* payload, size_t length) {
		if (state == State::closed) {
			return ResultStatus::noConnection;
		}

		std::array<uint8, 14> header{};
		size_t headerLength = 0;
		header[0] = static_cast<uint8>((fin ? 0x80 : 0x00) | opcode);

		uint64 wideLength = length;
		if (wideLength < 126) {
			header[1] = static_cast<uint8>(0x80 | wideLength);
			headerLength = 2;
		} else if (wideLength <= 0xFFFF) {
			header[1] = static_cast<uint8>(0x80 | 126);
			header[2] = static_cast<uint8>(wideLength >> 8);
			header[3] = static_cast<uint8>(wideLength);
			headerLength = 4;
		} else {
			header[1] = static_cast<uint8>(0x80 | 127);
			for (size_t i = 0; i < 8; i++) {
				header[2 + i] = static_cast<uint8>(wideLength >> (56 - 8 * i));
			}

			headerLength = 10;
		}

		uint32 keyBits = randomSource();
		std::array<uint8, 4> mask = {
			static_cast<uint8>(keyBits >> 24), static_cast<uint8>(keyBits >> 16),
			static_cast<uint8>(keyBits >> 8), static_cast<uint8>(keyBits)
		};

		for (size_t i = 0; i < mask.size(); i++) {
			header[headerLength + i] = mask[i];
		}
		headerLength += mask.size();

		txLength = 0;
		txStatus = ResultStatus::ok;
		for (size_t i = 0; i < headerLength; i++) {
			txBuffer[txLength++] = header[i];
		}

		for (size_t i = 0; i < length; i++) {
			if (txLength == txBuffer.size()) {
				ResultStatus status = FlushTx();
				if (status != ResultStatus::ok) {
					return status;
				}
			}

			txBuffer[txLength++] = payload[i] ^ mask[i & 3];
		}

		return FlushTx();
	}


	// Reads at most one socket chunk when the buffer is empty, then processes everything buffered.
	ResultStatus ReceiveOnce() {
		if (rxPos >= rxLen) {
			Result<uint32> result = adapter.ReadByteArray(rxBuffer.data(), static_cast<uint32>(rxBuffer.size()));
			if (result.IsErr()) {
				if (result.Error() == ResultStatus::timeout) {
					return ResultStatus::timeout;
				}

				Finish(1006, std::string_view(), result.Error());
				return lastStatus;
			}

			rxPos = 0;
			rxLen = result.Value();
		}

		return ConsumeReceived();
	}


	ResultStatus ConsumeReceived() {
		while (rxPos < rxLen) {
			ResultStatus status = ResultStatus::ok;
			if (frameStage == FrameStage::payload) {
				status = ParsePayloadBytes();
			} else {
				status = ParseHeaderBytes();
			}

			if (status != ResultStatus::ok) {
				return status;
			}

			if (state == State::closed) {
				return lastStatus;
			}
		}

		return ResultStatus::ok;
	}


	ResultStatus ParseHeaderBytes() {
		while (frameHeadFill < frameHeadNeeded && rxPos < rxLen) {
			frameHead[frameHeadFill++] = rxBuffer[rxPos++];
		}

		if (frameHeadFill < frameHeadNeeded) {
			return ResultStatus::ok;
		}

		if (frameStage == FrameStage::header) {
			return StartFrame();
		}

		return FinishLengthField();
	}


	ResultStatus StartFrame() {
		uint8 first = frameHead[0];
		uint8 second = frameHead[1];
		uint8 opcode = first & 0x0F;

		if ((first & 0x70) != 0) {
			return Fail(1002, ResultStatus::protocolError);
		}

		// Server frames are never masked (RFC 6455 5.1).
		if ((second & 0x80) != 0) {
			return Fail(1002, ResultStatus::protocolError);
		}

		if (!IsKnownOpcode(opcode)) {
			return Fail(1002, ResultStatus::protocolError);
		}

		uint8 lengthField = second & 0x7F;
		if (IsControlOpcode(opcode)) {
			if ((first & 0x80) == 0 || lengthField > kMaxControlPayload) {
				return Fail(1002, ResultStatus::protocolError);
			}
		} else if (opcode == 0x0) {
			if (!messageInProgress) {
				return Fail(1002, ResultStatus::protocolError);
			}
		} else if (messageInProgress) {
			// A new data message cannot start before the fragmented one ends.
			return Fail(1002, ResultStatus::protocolError);
		}

		frameFin = (first & 0x80) != 0;
		frameOpcode = opcode;
		frameOffset = 0;

		if (lengthField < 126) {
			frameLength = lengthField;
			return BeginPayload();
		}

		frameStage = FrameStage::extendedLength;
		frameHeadNeeded = (lengthField == 126) ? 4 : 10;
		return ResultStatus::ok;
	}


	// RFC 6455 5.2: the length must use the minimal encoding and the 64-bit MSB must be 0.
	ResultStatus FinishLengthField() {
		if (frameHeadNeeded == 4) {
			frameLength = (static_cast<uint64>(frameHead[2]) << 8) | frameHead[3];
			if (frameLength < 126) {
				return Fail(1002, ResultStatus::protocolError);
			}
		} else {
			if ((frameHead[2] & 0x80) != 0) {
				return Fail(1002, ResultStatus::protocolError);
			}

			frameLength = 0;
			for (size_t i = 2; i < 10; i++) {
				frameLength = (frameLength << 8) | frameHead[i];
			}

			if (frameLength <= 0xFFFF) {
				return Fail(1002, ResultStatus::protocolError);
			}
		}

		return BeginPayload();
	}


	ResultStatus BeginPayload() {
		frameStage = FrameStage::payload;
		frameOffset = 0;
		frameHeadFill = 0;
		frameHeadNeeded = 2;

		if (!IsControlOpcode(frameOpcode)) {
			if (frameOpcode != 0x0) {
				messageInProgress = true;
				messageOpcode = frameOpcode;
				messageLength = 0;
				utf8 = Utf8Checker();
			}

			uint64 total = static_cast<uint64>(messageLength) + frameLength;
			if (total > maxMessageSize) {
				return Fail(1009, ResultStatus::messageTooLong);
			}

			ResultStatus status = EnsureMessageCapacity(static_cast<size_t>(total));
			if (status != ResultStatus::ok) {
				return Fail(1011, status);
			}
		}

		if (frameLength == 0) {
			return FinishFrame();
		}

		return ResultStatus::ok;
	}


	ResultStatus ParsePayloadBytes() {
		uint64 remaining = frameLength - frameOffset;
		size_t available = rxLen - rxPos;
		size_t count = (remaining < available) ? static_cast<size_t>(remaining) : available;
		const uint8* data = rxBuffer.data() + rxPos;

		ResultStatus status = ResultStatus::ok;
		if (IsControlOpcode(frameOpcode)) {
			std::memcpy(controlPayload.data() + frameOffset, data, count);
		} else {
			status = AppendMessageBytes(data, count);
		}

		rxPos += count;
		frameOffset += count;
		if (status != ResultStatus::ok) {
			return status;
		}

		if (frameOffset == frameLength) {
			return FinishFrame();
		}

		return ResultStatus::ok;
	}


	ResultStatus AppendMessageBytes(const uint8* data, size_t count) {
		if (messageOpcode == 0x1) {
			for (size_t i = 0; i < count; i++) {
				if (!utf8.Feed(data[i])) {
					return Fail(1007, ResultStatus::invalidData);
				}
			}
		}

		// Re-resolve the handle: the allocator may move the block between calls (defragmentation).
		uint8* destination = static_cast<uint8*>(messageHandle.Data());
		std::memcpy(destination + messageLength, data, count);
		messageLength += count;
		return ResultStatus::ok;
	}


	// Grows the message buffer to hold needed bytes. Growth doubles when the allocator can give that much.
	ResultStatus EnsureMessageCapacity(size_t needed) {
		if (needed == 0 || (messageHandle.IsValid() && messageHandle.Size() >= needed)) {
			return ResultStatus::ok;
		}

		size_t current = messageHandle.IsValid() ? messageHandle.Size() : 0;
		size_t target = std::max(needed, current * 2);
		target = std::min(target, static_cast<size_t>(maxMessageSize));

		VSTD::Handle grown = Resize(target);
		if (!grown.IsValid() && target > needed) {
			grown = Resize(needed);
		}

		if (!grown.IsValid()) {
			return ResultStatus::outOfMemory;
		}

		messageHandle = grown;
		return ResultStatus::ok;
	}


	VSTD::Handle Resize(size_t size) {
		if (messageHandle.IsValid()) {
			return memory.Reallocate(messageHandle, size);
		}

		return memory.Allocate(size);
	}


	ResultStatus FinishFrame() {
		frameStage = FrameStage::header;
		frameHeadFill = 0;
		frameHeadNeeded = 2;

		if (IsControlOpcode(frameOpcode)) {
			return HandleControl();
		}

		if (!frameFin) {
			return ResultStatus::ok;
		}

		if (messageOpcode == 0x1 && !utf8.Complete()) {
			return Fail(1007, ResultStatus::invalidData);
		}

		uint8 opcode = messageOpcode;
		size_t length = messageLength;
		messageInProgress = false;
		messageOpcode = 0;
		messageLength = 0;

		const uint8* data = static_cast<const uint8*>(messageHandle.Data());
		if (length == 0 || data == nullptr) {
			Deliver(opcode, nullptr, 0);
		} else {
			Deliver(opcode, data, length);
		}

		if (messageHandle.IsValid()) {
			memory.Deallocate(messageHandle);
		}

		// A callback cannot close the connection; if a send inside it failed, ConsumeReceived sees the closed state.
		return ResultStatus::ok;
	}


	void Deliver(uint8 opcode, const uint8* data, size_t length) {
		if (opcode == 0x1) {
			if (onText) {
				onText(length == 0 ? std::string_view() : std::string_view(reinterpret_cast<const char*>(data), length));
			}
		} else if (onBinary) {
			onBinary(length == 0 ? std::span<const uint8>() : std::span<const uint8>(data, length));
		}
	}


	ResultStatus HandleControl() {
		size_t length = static_cast<size_t>(frameLength);

		if (frameOpcode == 0x9) {
			// The pong carries the ping's payload unchanged (RFC 6455 5.5.3).
			return SendFrame(0xA, true, controlPayload.data(), length);
		}

		if (frameOpcode == 0xA) {
			return ResultStatus::ok;
		}

		return HandleClose(length);
	}


	ResultStatus HandleClose(size_t length) {
		if (length == 1) {
			return Fail(1002, ResultStatus::protocolError);
		}

		uint16 code = 1005;
		std::string_view reason;
		if (length >= 2) {
			code = static_cast<uint16>((controlPayload[0] << 8) | controlPayload[1]);
			if (!IsSendableCloseCode(code)) {
				return Fail(1002, ResultStatus::protocolError);
			}

			reason = std::string_view(reinterpret_cast<const char*>(controlPayload.data() + 2), length - 2);
			if (!IsValidUtf8(reason)) {
				return Fail(1007, ResultStatus::invalidData);
			}
		}

		peerCloseReceived = true;

		if (state == State::open) {
			// Echo the status code (RFC 6455 5.5.1). An absent code is echoed as an empty payload.
			std::array<uint8, 2> echo = {
				static_cast<uint8>(code >> 8), static_cast<uint8>(code)
			};
			size_t echoLength = (code == 1005) ? 0 : 2;
			ResultStatus status = SendFrame(0x8, true, echo.data(), echoLength);
			if (state == State::closed) {
				return lastStatus;
			}

			if (status != ResultStatus::ok) {
				Finish(1006, std::string_view(), status);
				return status;
			}
		}

		Finish(code, reason, ResultStatus::connectionClosed);
		return lastStatus;
	}


	// Protocol error: send the close code, then fail the connection without waiting for the peer.
	ResultStatus Fail(uint16 code, ResultStatus status) {
		if (state == State::open) {
			std::array<uint8, 2> payload = {
				static_cast<uint8>(code >> 8), static_cast<uint8>(code)
			};
			SendFrame(0x8, true, payload.data(), payload.size());
		}

		Finish(code, std::string_view(), status);
		return status;
	}


	// Ends the connection once: releases the message buffer, closes the socket and reports onClose.
	void Finish(uint16 code, std::string_view reason, ResultStatus status) {
		if (state == State::closed) {
			return;
		}

		state = State::closed;
		closeCode = code;
		closeReasonLength = std::min(reason.size(), kMaxCloseReason);
		if (closeReasonLength > 0) {
			std::memcpy(closeReason.data(), reason.data(), closeReasonLength);
		}

		messageInProgress = false;
		messageOpcode = 0;
		messageLength = 0;
		if (messageHandle.IsValid()) {
			memory.Deallocate(messageHandle);
		}

		frameStage = FrameStage::header;
		frameHeadFill = 0;
		frameHeadNeeded = 2;
		txLength = 0;

		adapter.Close();
		lastStatus = status;

		if (onClose) {
			onClose(closeCode, GetCloseReason());
		}
	}
};
