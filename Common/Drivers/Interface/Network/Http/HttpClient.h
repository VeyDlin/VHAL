#pragma once
#include <Adapter/SocketAdapter.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Memory/IAllocator.h>
#include <Utilities/Callback/Ref.h>
#include "HttpByteBuffer.h"
#include <cstddef>
#include <span>
#include <string_view>


// HTTP/1.0 and HTTP/1.1 client over a SocketAdapter. Usage is Send(), then ReadBody() or ReadBodyTo(). The body is
// streamed: it is never buffered whole. Redirects are not followed. See the wiki page for the status table.
class HttpClient {
public:
	enum class Method : uint8 {
		Get,
		Head,
		Post,
		Put,
		Patch,
		Delete
	};

	enum class Scheme : uint8 {
		Http,
		Https
	};

	struct Header {
		std::string_view name;
		std::string_view value;
	};

	struct Request {
		Method method = Method::Get;
		std::string_view url;
		std::span<const Header> headers;
		std::span<const uint8> body;
	};

	struct UrlParts {
		Scheme scheme = Scheme::Http;
		std::string_view host;
		uint16 port = 80;
		// Path and query as they go into the request line. Empty means "/".
		std::string_view target;
	};

	struct Options {
		// Limit for the TCP connect and the TLS handshake. Must be above 0.
		uint32 connectTimeoutMs = 10000;
		// Limit for one read and for one write, applied to the open connection. Used by reads of the response and by
		// the request write.
		uint32 timeoutMs = 10000;
		// Limit for the status line and the header block of one response, including interim responses.
		uint32 maxHeaderBytes = 4096;
		// Https only. nullptr uses the certificate set built into the socket port.
		const char *caCertificate = nullptr;
		// Sent unless the request supplies its own User-Agent. nullptr sends none.
		const char *userAgent = "VHAL-HttpClient/1.0";
	};

	Options options;

private:
	enum class BodyMode : uint8 {
		None,
		Length,
		Chunked,
		UntilClose
	};

	enum class ChunkState : uint8 {
		Size,
		Extension,
		Data,
		DataEnd,
		Trailer,
		Done
	};

	static constexpr uint32 ReceiveBytes = 512;
	static constexpr uint32 SendBytes = 256;
	static constexpr uint32 MaxHostBytes = 128;
	static constexpr uint64 MaxUint64 = 0xFFFFFFFFFFFFFFFFull;
	static constexpr std::size_t MaxBodyBytes = 0xFFFFFFFFu;

	SocketAdapter& socket;
	HttpByteBuffer head;
	uint8 receiveBuffer[ReceiveBytes] = {};
	uint32 receivePosition = 0;
	uint32 receiveEnd = 0;
	uint8 sendBuffer[SendBytes] = {};
	uint32 sendSize = 0;
	ResultStatus sendStatus = ResultStatus::ok;

	char connectedHost[MaxHostBytes] = {};
	uint16 connectedPort = 0;
	Scheme connectedScheme = Scheme::Http;
	bool connectionReusable = false;
	bool bodyPending = false;
	uint32 responseBytes = 0;

	uint16 statusCode = 0;
	bool http10 = false;
	BodyMode bodyMode = BodyMode::None;
	uint64 bodyRemaining = 0;
	bool keepAlive = false;

	ChunkState chunkState = ChunkState::Size;
	uint64 chunkSize = 0;
	uint64 chunkRemaining = 0;
	uint32 chunkDigits = 0;
	uint32 trailerLine = 0;
	uint32 trailerBytes = 0;

public:
	HttpClient(SocketAdapter& adapter, Memory::IAllocator& memory)
		: socket(adapter),
		  head(memory) {
	}


	~HttpClient() {
		Close();
	}


	HttpClient(const HttpClient&) = delete;
	HttpClient& operator=(const HttpClient&) = delete;


	// Splits an absolute http:// or https:// URL. Views point into url.
	static ResultStatus ParseUrl(std::string_view url, UrlParts& parts) {
		for (char character : url) {
			unsigned char code = static_cast<unsigned char>(character);
			if (code <= 0x20 || code == 0x7F) {
				return ResultStatus::invalidParameter;
			}
		}

		std::string_view rest;
		if (StartsWithIgnoreCase(url, "http://")) {
			parts.scheme = Scheme::Http;
			parts.port = 80;
			rest = url.substr(7);
		} else if (StartsWithIgnoreCase(url, "https://")) {
			parts.scheme = Scheme::Https;
			parts.port = 443;
			rest = url.substr(8);
		} else if (url.find("://") != std::string_view::npos) {
			return ResultStatus::notSupported;
		} else {
			return ResultStatus::invalidParameter;
		}

		std::size_t authorityEnd = rest.find_first_of("/?#");
		std::string_view authority = rest.substr(0, authorityEnd);
		std::string_view remainder;
		if (authorityEnd != std::string_view::npos) {
			remainder = rest.substr(authorityEnd);
		}

		if (authority.find('@') != std::string_view::npos) {
			return ResultStatus::notSupported;
		}

		// Checked before the port: the colons of an IPv6 literal are not a port separator.
		if (!authority.empty() && authority.front() == '[') {
			return ResultStatus::notSupported;
		}

		std::string_view hostPart = authority;
		std::size_t colon = authority.rfind(':');
		if (colon != std::string_view::npos) {
			hostPart = authority.substr(0, colon);
			uint32 port = 0;
			if (!ParseUnsigned(authority.substr(colon + 1), 65535, port) || port == 0) {
				return ResultStatus::invalidParameter;
			}

			parts.port = static_cast<uint16>(port);
		}

		if (hostPart.empty()) {
			return ResultStatus::invalidParameter;
		}

		if (hostPart.size() >= MaxHostBytes) {
			return ResultStatus::outOfRange;
		}

		for (char character : hostPart) {
			if (!IsHostCharacter(character)) {
				return ResultStatus::invalidParameter;
			}
		}

		std::size_t fragment = remainder.find('#');
		if (fragment != std::string_view::npos) {
			remainder = remainder.substr(0, fragment);
		}

		parts.host = hostPart;
		parts.target = remainder;
		return ResultStatus::ok;
	}


	// Connects if needed, writes the request and reads the final response head. Interim 1xx responses are skipped.
	// Afterwards the status code and headers are available, and the body is read with ReadBody() or ReadBodyTo().
	// A body that was not read to its end is discarded: the connection is closed before the next request.
	ResultStatus Send(const Request& request) {
		UrlParts parts;
		ResultStatus valid = ValidateRequest(request, parts);
		if (valid != ResultStatus::ok) {
			return valid;
		}

		if (bodyPending) {
			Close();
		}

		bool reused = socket.IsOpen() && connectionReusable && IsSameEndpoint(parts);
		ResultStatus status = reused ? ResultStatus::ok : Connect(parts);
		if (status == ResultStatus::ok) {
			status = Exchange(request, parts);

			// A reused connection the server closed while idle fails before any response byte arrives. The request
			// is sent once more on a new connection. A body POSTed on the stale connection is not seen by the server
			// in that case, so the retry does not duplicate it.
			if (reused && IsStale(status) && responseBytes == 0) {
				status = Connect(parts);
				if (status == ResultStatus::ok) {
					status = Exchange(request, parts);
				}
			}
		}

		if (status != ResultStatus::ok) {
			Close();
			return status;
		}

		bodyPending = true;
		return ResultStatus::ok;
	}


	// Streams the rest of the body to onBody. Returns ok at the end of the body, connectionClosed when the peer closed
	// before the body was complete, or the status of a failed read or of the callback. onBody receives each piece of
	// the body as it arrives; the span is valid only during the call. Returning anything other than ok stops the read:
	// the connection is closed and ReadBody() returns that status. A null function pointer returns invalidParameter.
	ResultStatus ReadBody(Callback::Ref<ResultStatus(std::span<const uint8>)> onBody) {
		if (!onBody) {
			return ResultStatus::invalidParameter;
		}

		if (!bodyPending) {
			return ResultStatus::notReady;
		}

		ResultStatus status = ResultStatus::ok;
		switch (bodyMode) {
			case BodyMode::None:
				break;
			case BodyMode::Length:
				status = ReadLength(onBody);
				break;
			case BodyMode::Chunked:
				status = ReadChunked(onBody);
				break;
			case BodyMode::UntilClose:
				status = ReadUntilClose(onBody);
				break;
		}

		bodyPending = false;
		bool leftover = receivePosition < receiveEnd;
		if (status != ResultStatus::ok || !keepAlive || leftover) {
			Close();
		} else {
			connectionReusable = true;
		}

		return status;
	}


	// Reads the whole body into buffer. size receives the number of bytes stored. outOfRange when the body does not fit,
	// and the rest of the body is then discarded with the connection.
	ResultStatus ReadBodyTo(std::span<uint8> buffer, uint32& size) {
		size = 0;
		return ReadBody([&buffer, &size](std::span<const uint8> piece) -> ResultStatus {
			if (piece.size() > buffer.size() - size) {
				return ResultStatus::outOfRange;
			}

			for (std::size_t index = 0; index < piece.size(); index++) {
				buffer[size] = piece[index];
				size++;
			}

			return ResultStatus::ok;
		});
	}


	// Closes the connection and forgets any unread body. Safe to call when nothing is open.
	ResultStatus Close() {
		bodyPending = false;
		connectionReusable = false;
		receivePosition = 0;
		receiveEnd = 0;
		return socket.Close();
	}


	// Status code of the final response, 0 before the first Send().
	uint16 GetStatusCode() const {
		return statusCode;
	}


	// Looks up a response header by name, ignoring case. The first occurrence wins. value is valid until the next Send(),
	// Close(), or a heap defragment step: call again after either.
	bool FindHeader(std::string_view name, std::string_view& value) const {
		std::string_view rest = SkipFirstLine(head.View());
		std::string_view line;
		while (NextLine(rest, line)) {
			if (line.empty()) {
				return false;
			}

			std::size_t colon = line.find(':');
			if (colon == std::string_view::npos) {
				continue;
			}

			if (EqualsIgnoreCase(line.substr(0, colon), name)) {
				value = Trim(line.substr(colon + 1));
				return true;
			}
		}

		return false;
	}


	bool IsConnected() const {
		return socket.IsOpen();
	}


private:
	static bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) {
		if (text.size() < prefix.size()) {
			return false;
		}

		return EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
	}


	static bool EqualsIgnoreCase(std::string_view left, std::string_view right) {
		if (left.size() != right.size()) {
			return false;
		}

		for (std::size_t index = 0; index < left.size(); index++) {
			if (ToLower(left[index]) != ToLower(right[index])) {
				return false;
			}
		}

		return true;
	}


	static char ToLower(char character) {
		if (character >= 'A' && character <= 'Z') {
			return static_cast<char>(character - 'A' + 'a');
		}

		return character;
	}


	static bool IsDigit(char character) {
		return character >= '0' && character <= '9';
	}


	static bool IsHostCharacter(char character) {
		if (IsDigit(character)) {
			return true;
		}

		char lower = ToLower(character);
		return (lower >= 'a' && lower <= 'z') || lower == '-' || lower == '.' || lower == '_';
	}


	// RFC 9110 token characters.
	static bool IsTokenCharacter(char character) {
		if (IsDigit(character)) {
			return true;
		}

		char lower = ToLower(character);
		if (lower >= 'a' && lower <= 'z') {
			return true;
		}

		switch (character) {
			case '!':
			case '#':
			case '$':
			case '%':
			case '&':
			case '\'':
			case '*':
			case '+':
			case '-':
			case '.':
			case '^':
			case '_':
			case '`':
			case '|':
			case '~':
				return true;
			default:
				return false;
		}
	}


	static bool IsToken(std::string_view text) {
		if (text.empty()) {
			return false;
		}

		for (char character : text) {
			if (!IsTokenCharacter(character)) {
				return false;
			}
		}

		return true;
	}


	// Field values may hold SP, HTAB and visible characters. CR, LF and NUL are not allowed, which also blocks
	// header injection through a caller's value.
	static bool IsFieldValue(std::string_view text) {
		for (char character : text) {
			unsigned char code = static_cast<unsigned char>(character);
			if (code == '\t') {
				continue;
			}

			if (code < 0x20 || code == 0x7F) {
				return false;
			}
		}

		return true;
	}


	static std::string_view Trim(std::string_view text) {
		while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
			text.remove_prefix(1);
		}

		while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
			text.remove_suffix(1);
		}

		return text;
	}


	// Reads an unsigned decimal of up to 19 digits. Fails on any other character, on a value above maximum, or on
	// overflow.
	static bool ParseUnsigned(std::string_view text, uint64 maximum, uint64& value) {
		if (text.empty() || text.size() > 19) {
			return false;
		}

		uint64 result = 0;
		for (char character : text) {
			if (!IsDigit(character)) {
				return false;
			}

			uint64 digit = static_cast<uint64>(character - '0');
			if (result > (MaxUint64 - digit) / 10) {
				return false;
			}

			result = result * 10 + digit;
		}

		if (result > maximum) {
			return false;
		}

		value = result;
		return true;
	}


	static bool ParseUnsigned(std::string_view text, uint32 maximum, uint32& value) {
		uint64 wide = 0;
		if (!ParseUnsigned(text, static_cast<uint64>(maximum), wide)) {
			return false;
		}

		value = static_cast<uint32>(wide);
		return true;
	}


	// Splits off the next line. The line excludes its LF and a CR before it. Lines end at LF, so a bare CR stays in
	// the line and is rejected by the caller where it matters.
	static bool NextLine(std::string_view& rest, std::string_view& line) {
		if (rest.empty()) {
			return false;
		}

		std::size_t end = rest.find('\n');
		std::string_view raw = (end == std::string_view::npos) ? rest : rest.substr(0, end);
		rest = (end == std::string_view::npos) ? std::string_view() : rest.substr(end + 1);
		if (!raw.empty() && raw.back() == '\r') {
			raw.remove_suffix(1);
		}

		line = raw;
		return true;
	}


	static std::string_view SkipFirstLine(std::string_view block) {
		std::string_view line;
		NextLine(block, line);
		return block;
	}


	static bool ListContains(std::string_view list, std::string_view token) {
		while (true) {
			std::size_t comma = list.find(',');
			if (EqualsIgnoreCase(Trim(list.substr(0, comma)), token)) {
				return true;
			}

			if (comma == std::string_view::npos) {
				return false;
			}

			list = list.substr(comma + 1);
		}
	}


	// The last non-empty item of a comma-separated list, or an empty view.
	static std::string_view LastListItem(std::string_view list) {
		std::string_view last;
		while (true) {
			std::size_t comma = list.find(',');
			std::string_view item = Trim(list.substr(0, comma));
			if (!item.empty()) {
				last = item;
			}

			if (comma == std::string_view::npos) {
				return last;
			}

			list = list.substr(comma + 1);
		}
	}


	// Content-Length may repeat or be a list, but every value must be the same (RFC 9110 section 8.6).
	static ResultStatus AddContentLength(std::string_view list, bool& seen, uint64& length) {
		bool found = false;
		while (true) {
			std::size_t comma = list.find(',');
			std::string_view item = Trim(list.substr(0, comma));
			if (!item.empty()) {
				uint64 number = 0;
				if (!ParseUnsigned(item, MaxUint64, number)) {
					return ResultStatus::invalidData;
				}

				if (seen && number != length) {
					return ResultStatus::invalidData;
				}

				seen = true;
				length = number;
				found = true;
			}

			if (comma == std::string_view::npos) {
				break;
			}

			list = list.substr(comma + 1);
		}

		return found ? ResultStatus::ok : ResultStatus::invalidData;
	}


	static const char *MethodName(Method method) {
		switch (method) {
			case Method::Get:
				return "GET";
			case Method::Head:
				return "HEAD";
			case Method::Post:
				return "POST";
			case Method::Put:
				return "PUT";
			case Method::Patch:
				return "PATCH";
			case Method::Delete:
				return "DELETE";
		}

		return "GET";
	}


	// Writes value in decimal into buffer and returns the digits.
	static std::string_view FormatDecimal(uint64 value, char (&buffer)[21]) {
		std::size_t index = sizeof(buffer);
		do {
			index--;
			buffer[index] = static_cast<char>('0' + value % 10);
			value /= 10;
		} while (value != 0);

		return std::string_view(buffer + index, sizeof(buffer) - index);
	}


	// A status that means the connection was not usable, as opposed to the server answering with an error.
	static bool IsStale(ResultStatus status) {
		return status == ResultStatus::writeError || status == ResultStatus::connectionClosed || status == ResultStatus::readError;
	}


	static ResultStatus ValidateRequest(const Request& request, UrlParts& parts) {
		ResultStatus parsed = ParseUrl(request.url, parts);
		if (parsed != ResultStatus::ok) {
			return parsed;
		}

		if (request.method == Method::Head && !request.body.empty()) {
			return ResultStatus::invalidParameter;
		}

		if (request.body.size() > MaxBodyBytes) {
			return ResultStatus::outOfRange;
		}

		for (const Header& header : request.headers) {
			if (!IsToken(header.name) || !IsFieldValue(header.value)) {
				return ResultStatus::invalidParameter;
			}

			// The client writes these itself, so a caller cannot make them disagree with the body or the connection.
			if (EqualsIgnoreCase(header.name, "Host") || EqualsIgnoreCase(header.name, "Content-Length") ||
				EqualsIgnoreCase(header.name, "Transfer-Encoding") || EqualsIgnoreCase(header.name, "Connection")) {
				return ResultStatus::invalidParameter;
			}
		}

		return ResultStatus::ok;
	}


	bool IsSameEndpoint(const UrlParts& parts) const {
		return connectedPort == parts.port && connectedScheme == parts.scheme &&
			EqualsIgnoreCase(std::string_view(connectedHost), parts.host);
	}


	ResultStatus Connect(const UrlParts& parts) {
		Close();
		for (std::size_t index = 0; index < parts.host.size(); index++) {
			connectedHost[index] = parts.host[index];
		}

		connectedHost[parts.host.size()] = '\0';
		connectedPort = parts.port;
		connectedScheme = parts.scheme;

		SocketAdapter::Parameters wanted;
		bool secure = parts.scheme == Scheme::Https;
		wanted.protocol = secure ? SocketAdapter::Protocol::Tls : SocketAdapter::Protocol::Tcp;
		wanted.caCertificate = secure ? options.caCertificate : nullptr;
		wanted.connectTimeoutMs = options.connectTimeoutMs;

		const SocketAdapter::Parameters& current = socket.GetParameters();
		if (current.protocol != wanted.protocol || current.caCertificate != wanted.caCertificate ||
			current.connectTimeoutMs != wanted.connectTimeoutMs) {
			ResultStatus applied = socket.SetParameters(wanted);
			if (applied != ResultStatus::ok) {
				return applied;
			}
		}

		socket.SetIoTimeout(options.timeoutMs);
		return socket.Open(connectedHost, connectedPort);
	}


	ResultStatus Exchange(const Request& request, const UrlParts& parts) {
		responseBytes = 0;
		ResultStatus written = WriteRequest(request, parts);
		if (written != ResultStatus::ok) {
			return written;
		}

		return ReadHead(request.method == Method::Head);
	}


	ResultStatus WriteRequest(const Request& request, const UrlParts& parts) {
		sendSize = 0;
		sendStatus = ResultStatus::ok;

		bool hasUserAgent = false;
		for (const Header& header : request.headers) {
			if (EqualsIgnoreCase(header.name, "User-Agent")) {
				hasUserAgent = true;
			}
		}

		AppendText(MethodName(request.method));
		AppendText(" ");
		if (parts.target.empty() || parts.target.front() == '?') {
			AppendText("/");
		}

		AppendText(parts.target);
		AppendText(" HTTP/1.1\r\nHost: ");
		AppendText(parts.host);
		bool defaultPort = (parts.scheme == Scheme::Https && parts.port == 443) || (parts.scheme == Scheme::Http && parts.port == 80);
		if (!defaultPort) {
			char portDigits[21] = {};
			AppendText(":");
			AppendText(FormatDecimal(parts.port, portDigits));
		}

		AppendText("\r\n");
		if (!hasUserAgent && options.userAgent != nullptr) {
			AppendText("User-Agent: ");
			AppendText(options.userAgent);
			AppendText("\r\n");
		}

		for (const Header& header : request.headers) {
			AppendText(header.name);
			AppendText(": ");
			AppendText(header.value);
			AppendText("\r\n");
		}

		bool sendsLength = !request.body.empty() || request.method == Method::Post || request.method == Method::Put ||
			request.method == Method::Patch;
		if (sendsLength) {
			char lengthDigits[21] = {};
			AppendText("Content-Length: ");
			AppendText(FormatDecimal(request.body.size(), lengthDigits));
			AppendText("\r\n");
		}

		AppendText("Connection: keep-alive\r\n\r\n");
		FlushSend();
		if (sendStatus != ResultStatus::ok) {
			return sendStatus;
		}

		if (!request.body.empty()) {
			return socket.WriteByteArray(request.body.data(), static_cast<uint32>(request.body.size()));
		}

		return ResultStatus::ok;
	}


	void AppendText(std::string_view text) {
		for (char character : text) {
			if (sendSize == SendBytes) {
				FlushSend();
			}

			sendBuffer[sendSize] = static_cast<uint8>(character);
			sendSize++;
		}
	}


	void FlushSend() {
		if (sendSize > 0 && sendStatus == ResultStatus::ok) {
			sendStatus = socket.WriteByteArray(sendBuffer, sendSize);
		}

		sendSize = 0;
	}


	// Pulls the next byte from the connection, refilling the receive buffer when it is empty.
	ResultStatus EnsureReceived() {
		if (receivePosition < receiveEnd) {
			return ResultStatus::ok;
		}

		Result<uint32> received = socket.ReadByteArray(receiveBuffer, ReceiveBytes);
		if (received.IsErr()) {
			return received.Error();
		}

		if (received.Value() == 0) {
			return ResultStatus::readError;
		}

		receivePosition = 0;
		receiveEnd = received.Value();
		return ResultStatus::ok;
	}


	void ResetResponse() {
		statusCode = 0;
		http10 = false;
		bodyMode = BodyMode::None;
		bodyRemaining = 0;
		keepAlive = false;
		chunkState = ChunkState::Size;
		chunkSize = 0;
		chunkRemaining = 0;
		chunkDigits = 0;
		trailerLine = 0;
		trailerBytes = 0;
	}


	ResultStatus ReadHead(bool headRequest) {
		ResetResponse();
		while (true) {
			ResultStatus block = ReadHeadBlock();
			if (block != ResultStatus::ok) {
				return block;
			}

			ResultStatus line = ParseStatusLine(head.View());
			if (line != ResultStatus::ok) {
				return line;
			}

			// 1xx responses other than 101 come before the final response. They carry no body and are skipped.
			if (statusCode >= 100 && statusCode < 200 && statusCode != 101) {
				head.Clear();
				continue;
			}

			return ParseHeaders(headRequest);
		}
	}


	// Reads bytes up to and including the blank line that ends the header block. Leading empty lines before the status
	// line are skipped, as RFC 9112 section 2.2 allows.
	ResultStatus ReadHeadBlock() {
		head.Clear();
		std::size_t lineStart = 0;
		while (true) {
			ResultStatus ready = EnsureReceived();
			if (ready != ResultStatus::ok) {
				return ready;
			}

			uint8 byte = receiveBuffer[receivePosition];
			receivePosition++;
			responseBytes++;

			ResultStatus appended = head.Append(byte, options.maxHeaderBytes);
			if (appended != ResultStatus::ok) {
				return appended;
			}

			if (byte != '\n') {
				continue;
			}

			std::size_t contentEnd = head.Size() - 1;
			std::string_view current = head.View();
			if (contentEnd > lineStart && current[contentEnd - 1] == '\r') {
				contentEnd--;
			}

			bool emptyLine = contentEnd == lineStart;
			if (emptyLine && lineStart == 0) {
				head.Clear();
				continue;
			}

			if (emptyLine) {
				return ResultStatus::ok;
			}

			lineStart = head.Size();
		}
	}


	// Sets statusCode and http10 from the first line of the head. The reason phrase is not checked.
	ResultStatus ParseStatusLine(std::string_view block) {
		std::string_view rest = block;
		std::string_view line;
		NextLine(rest, line);
		if (line.size() < 12 || line.substr(0, 5) != "HTTP/" || line[6] != '.') {
			return ResultStatus::invalidData;
		}

		bool digitsOk = IsDigit(line[5]) && IsDigit(line[7]) && IsDigit(line[9]) && IsDigit(line[10]) && IsDigit(line[11]);
		if (!digitsOk || line[8] != ' ') {
			return ResultStatus::invalidData;
		}

		if (line.size() > 12 && line[12] != ' ') {
			return ResultStatus::invalidData;
		}

		// HTTP/1.x with x above 1 is read as HTTP/1.1. Any other major version is not supported.
		if (line[5] != '1') {
			return ResultStatus::notSupported;
		}

		uint32 code = static_cast<uint32>((line[9] - '0') * 100 + (line[10] - '0') * 10 + (line[11] - '0'));
		if (code < 100) {
			return ResultStatus::invalidData;
		}

		statusCode = static_cast<uint16>(code);
		http10 = line[7] == '0';
		return ResultStatus::ok;
	}


	// Reads the fields that decide the body and the connection. Rejects what RFC 9112 requires to be rejected: obs-fold,
	// a header line without a colon, whitespace before the colon, and Content-Length that disagrees with itself or with
	// Transfer-Encoding.
	ResultStatus ParseHeaders(bool headRequest) {
		bool contentLengthSeen = false;
		uint64 contentLength = 0;
		bool transferEncodingSeen = false;
		bool finalChunked = false;
		bool closeToken = false;
		bool keepAliveToken = false;

		std::string_view rest = SkipFirstLine(head.View());
		std::string_view line;
		while (NextLine(rest, line) && !line.empty()) {
			if (line.front() == ' ' || line.front() == '\t') {
				return ResultStatus::invalidData;
			}

			std::size_t colon = line.find(':');
			if (colon == std::string_view::npos) {
				return ResultStatus::invalidData;
			}

			std::string_view name = line.substr(0, colon);
			std::string_view value = Trim(line.substr(colon + 1));
			if (!IsToken(name) || !IsFieldValue(value)) {
				return ResultStatus::invalidData;
			}

			if (EqualsIgnoreCase(name, "Content-Length")) {
				ResultStatus length = AddContentLength(value, contentLengthSeen, contentLength);
				if (length != ResultStatus::ok) {
					return length;
				}
			} else if (EqualsIgnoreCase(name, "Transfer-Encoding")) {
				transferEncodingSeen = true;
				finalChunked = EqualsIgnoreCase(LastListItem(value), "chunked");
			} else if (EqualsIgnoreCase(name, "Connection")) {
				closeToken = closeToken || ListContains(value, "close");
				keepAliveToken = keepAliveToken || ListContains(value, "keep-alive");
			}
		}

		// RFC 9112 section 6.1 says Transfer-Encoding overrides Content-Length. A message with both is a request
		// smuggling shape, so it is refused instead of guessed at.
		bool transferEncoding = transferEncodingSeen && !http10;
		if (transferEncoding && contentLengthSeen) {
			return ResultStatus::invalidData;
		}

		bodyRemaining = 0;
		if (headRequest || statusCode == 101 || statusCode == 204 || statusCode == 304) {
			bodyMode = BodyMode::None;
		} else if (transferEncoding) {
			bodyMode = finalChunked ? BodyMode::Chunked : BodyMode::UntilClose;
		} else if (contentLengthSeen) {
			bodyMode = BodyMode::Length;
			bodyRemaining = contentLength;
		} else {
			bodyMode = BodyMode::UntilClose;
		}

		bool reusable = !closeToken && statusCode != 101 && bodyMode != BodyMode::UntilClose;
		if (http10) {
			reusable = reusable && keepAliveToken;
		}

		keepAlive = reusable;
		return ResultStatus::ok;
	}


	ResultStatus ReadLength(Callback::Ref<ResultStatus(std::span<const uint8>)> onBody) {
		while (bodyRemaining > 0) {
			ResultStatus ready = EnsureReceived();
			if (ready != ResultStatus::ok) {
				return ready;
			}

			uint32 available = receiveEnd - receivePosition;
			uint32 count = (bodyRemaining < available) ? static_cast<uint32>(bodyRemaining) : available;
			ResultStatus delivered = onBody(std::span<const uint8>(receiveBuffer + receivePosition, count));
			receivePosition += count;
			bodyRemaining -= count;
			if (delivered != ResultStatus::ok) {
				return delivered;
			}
		}

		return ResultStatus::ok;
	}


	// Without a length the body ends when the server closes the connection (RFC 9112 section 6.3, rule 7).
	ResultStatus ReadUntilClose(Callback::Ref<ResultStatus(std::span<const uint8>)> onBody) {
		while (true) {
			ResultStatus ready = EnsureReceived();
			if (ready == ResultStatus::connectionClosed) {
				return ResultStatus::ok;
			}

			if (ready != ResultStatus::ok) {
				return ready;
			}

			uint32 count = receiveEnd - receivePosition;
			ResultStatus delivered = onBody(std::span<const uint8>(receiveBuffer + receivePosition, count));
			receivePosition += count;
			if (delivered != ResultStatus::ok) {
				return delivered;
			}
		}
	}


	ResultStatus ReadChunked(Callback::Ref<ResultStatus(std::span<const uint8>)> onBody) {
		while (chunkState != ChunkState::Done) {
			ResultStatus ready = EnsureReceived();
			if (ready != ResultStatus::ok) {
				return ready;
			}

			if (chunkState == ChunkState::Data) {
				uint32 available = receiveEnd - receivePosition;
				uint32 count = (chunkRemaining < available) ? static_cast<uint32>(chunkRemaining) : available;
				ResultStatus delivered = onBody(std::span<const uint8>(receiveBuffer + receivePosition, count));
				receivePosition += count;
				chunkRemaining -= count;
				if (chunkRemaining == 0) {
					chunkState = ChunkState::DataEnd;
				}

				if (delivered != ResultStatus::ok) {
					return delivered;
				}
			} else {
				uint8 byte = receiveBuffer[receivePosition];
				receivePosition++;
				ResultStatus framed = ConsumeChunkFraming(byte);
				if (framed != ResultStatus::ok) {
					return framed;
				}
			}
		}

		return ResultStatus::ok;
	}


	// Consumes one byte outside chunk data: the size line, the CRLF after data, and the trailer section.
	// Chunk extensions are skipped without being read.
	ResultStatus ConsumeChunkFraming(uint8 byte) {
		switch (chunkState) {
			case ChunkState::Size:
				if (IsHexDigit(byte)) {
					uint64 digit = HexValue(byte);
					if (chunkSize > (MaxUint64 - digit) / 16) {
						return ResultStatus::invalidData;
					}

					chunkSize = chunkSize * 16 + digit;
					chunkDigits++;
					return ResultStatus::ok;
				}

				if (byte == '\n') {
					return EndChunkSizeLine();
				}

				if (byte == ';' || byte == ' ' || byte == '\t' || byte == '\r') {
					chunkState = ChunkState::Extension;
					return ResultStatus::ok;
				}

				return ResultStatus::invalidData;
			case ChunkState::Extension:
				if (byte == '\n') {
					return EndChunkSizeLine();
				}

				return ResultStatus::ok;
			case ChunkState::DataEnd:
				if (byte == '\r') {
					return ResultStatus::ok;
				}

				if (byte == '\n') {
					chunkState = ChunkState::Size;
					return ResultStatus::ok;
				}

				return ResultStatus::invalidData;
			case ChunkState::Trailer:
				return ConsumeTrailerByte(byte);
			case ChunkState::Data:
			case ChunkState::Done:
				return ResultStatus::invalidData;
		}

		return ResultStatus::invalidData;
	}


	ResultStatus EndChunkSizeLine() {
		if (chunkDigits == 0) {
			return ResultStatus::invalidData;
		}

		chunkRemaining = chunkSize;
		chunkSize = 0;
		chunkDigits = 0;
		if (chunkRemaining == 0) {
			chunkState = ChunkState::Trailer;
			trailerLine = 0;
		} else {
			chunkState = ChunkState::Data;
		}

		return ResultStatus::ok;
	}


	// Trailer fields are skipped. They count against maxHeaderBytes so a server cannot stream them forever.
	ResultStatus ConsumeTrailerByte(uint8 byte) {
		if (byte == '\r') {
			return ResultStatus::ok;
		}

		if (byte == '\n') {
			if (trailerLine == 0) {
				chunkState = ChunkState::Done;
			}

			trailerLine = 0;
			return ResultStatus::ok;
		}

		trailerLine++;
		trailerBytes++;
		if (trailerBytes > options.maxHeaderBytes) {
			return ResultStatus::outOfRange;
		}

		return ResultStatus::ok;
	}


	static bool IsHexDigit(uint8 byte) {
		return IsDigit(static_cast<char>(byte)) || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
	}


	static uint64 HexValue(uint8 byte) {
		if (IsDigit(static_cast<char>(byte))) {
			return static_cast<uint64>(byte - '0');
		}

		if (byte >= 'a' && byte <= 'f') {
			return static_cast<uint64>(byte - 'a' + 10);
		}

		return static_cast<uint64>(byte - 'A' + 10);
	}
};
