#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/VSTD/memory/IAllocator.h>
#include <Utilities/VSTD/utility/Function.h>
#include "HttpByteBuffer.h"
#include <cstddef>
#include <span>
#include <string_view>


// Server-Sent Events parser following the WHATWG HTML "Server-sent events" section. It takes bytes from any transport,
// split at any point, and calls onEvent once per dispatched event. It does not know about HTTP.
//
// Memory: the event type, the data and the last event ID are blocks from the allocator, each limited to maxEventBytes.
// The views passed to onEvent point into those blocks. They are valid only during the call, and the heap must not run
// a defragment step inside onEvent.
class ServerSentEvents {
public:
	using EventCallback = VSTD::Function<void(std::string_view, std::string_view, std::string_view)>;

	EventCallback onEvent;

private:
	enum class Field : uint8 {
		Ignored,
		Data,
		Event,
		Id,
		Retry
	};

	static constexpr std::size_t NameCapacity = 8;
	static constexpr uint32 MaxRetryValue = 0xFFFFFFFFu;
	static constexpr uint8 Bom[3] = { 0xEF, 0xBB, 0xBF };

	VSTD::IAllocator& allocator;
	uint32 maxEventBytes;
	HttpByteBuffer eventType;
	HttpByteBuffer data;
	HttpByteBuffer lineValue;
	HttpByteBuffer lastEventId;

	// Stream state: a BOM at the very start, and a CR that may be the first half of a CRLF split across Feed() calls.
	bool bomPending = true;
	uint32 bomCount = 0;
	uint8 bomSeen[3] = {};
	bool afterCarriageReturn = false;

	// Line state.
	bool lineHasBytes = false;
	bool inValue = false;
	bool skipSpace = false;
	Field field = Field::Ignored;
	char name[NameCapacity] = {};
	std::size_t nameLength = 0;
	bool lineOverflow = false;
	bool nulSeen = false;
	bool retryValid = true;
	uint32 retryDigits = 0;
	uint32 retryValue = 0;

	// Event state.
	bool dropping = false;
	ResultStatus dropReport = ResultStatus::ok;
	bool hasRetry = false;
	uint32 retry = 0;

	// Streaming UTF-8 decoder for the value bytes. Invalid sequences become U+FFFD, as the WHATWG decoder does.
	uint8 utfNeeded = 0;
	uint8 utfLength = 0;
	uint8 utfLower = 0x80;
	uint8 utfUpper = 0xBF;
	uint8 utfBytes[4] = {};

public:
	ServerSentEvents(VSTD::IAllocator& memory, uint32 maxEventBytesValue)
		: allocator(memory),
		  maxEventBytes(maxEventBytesValue),
		  eventType(memory),
		  data(memory),
		  lineValue(memory),
		  lastEventId(memory) {
	}


	ServerSentEvents(const ServerSentEvents&) = delete;
	ServerSentEvents& operator=(const ServerSentEvents&) = delete;


	// Feeds the next bytes of the stream. Returns ok, or the reason of the last event dropped during this call
	// (outOfRange when it exceeded maxEventBytes, outOfMemory when the heap could not hold it). A dropped event is not
	// dispatched; the parser resyncs at the next blank line, so the caller keeps feeding.
	ResultStatus Feed(std::span<const uint8> bytes) {
		dropReport = ResultStatus::ok;
		for (std::size_t index = 0; index < bytes.size(); index++) {
			Process(bytes[index]);
		}

		ResultStatus report = dropReport;
		dropReport = ResultStatus::ok;
		return report;
	}


	// Starts a new stream: forgets the last event ID, the retry value and any partial event.
	void Reset() {
		eventType.Clear();
		data.Clear();
		lastEventId.Clear();
		ResetLine();
		ResetSequence();
		bomPending = true;
		bomCount = 0;
		afterCarriageReturn = false;
		dropping = false;
		hasRetry = false;
		retry = 0;
	}


	// The last event ID, which persists across events until the stream sets another one.
	std::string_view GetLastEventId() const {
		return lastEventId.View();
	}


	bool HasRetry() const {
		return hasRetry;
	}


	// Reconnection time from a "retry" field, in milliseconds. Valid only when HasRetry() is true.
	uint32 GetRetry() const {
		return retry;
	}


private:
	void Process(uint8 byte) {
		if (bomPending) {
			HandleBom(byte);
			return;
		}

		if (afterCarriageReturn) {
			afterCarriageReturn = false;
			if (byte == '\n') {
				return;
			}
		}

		if (byte == '\r') {
			EndLine();
			afterCarriageReturn = true;
			return;
		}

		if (byte == '\n') {
			EndLine();
			return;
		}

		AddLineByte(byte);
	}


	// Only a BOM that starts the stream is removed. Bytes that look like the start of a BOM but are not one are replayed.
	void HandleBom(uint8 byte) {
		bomSeen[bomCount] = byte;
		bomCount++;
		if (bomSeen[bomCount - 1] == Bom[bomCount - 1]) {
			if (bomCount == 3) {
				bomPending = false;
				bomCount = 0;
			}
			return;
		}

		bomPending = false;
		for (uint32 index = 0; index < bomCount; index++) {
			Process(bomSeen[index]);
		}

		bomCount = 0;
	}


	void AddLineByte(uint8 byte) {
		lineHasBytes = true;
		if (!inValue) {
			if (byte == ':') {
				field = ClassifyName();
				inValue = true;
				skipSpace = true;
				return;
			}

			if (nameLength < NameCapacity) {
				name[nameLength] = static_cast<char>(byte);
			}

			if (nameLength <= NameCapacity) {
				nameLength++;
			}

			return;
		}

		if (skipSpace) {
			skipSpace = false;
			if (byte == ' ') {
				return;
			}
		}

		AddValueByte(byte);
	}


	void AddValueByte(uint8 byte) {
		switch (field) {
			case Field::Ignored:
				return;
			case Field::Retry:
				AddRetryByte(byte);
				return;
			case Field::Id:
				if (byte == 0) {
					nulSeen = true;
					return;
				}

				DecodeByte(byte);
				return;
			case Field::Data:
			case Field::Event:
				DecodeByte(byte);
				return;
		}
	}


	void AddRetryByte(uint8 byte) {
		if (byte < '0' || byte > '9') {
			retryValid = false;
			return;
		}

		uint32 digit = static_cast<uint32>(byte - '0');
		retryDigits++;
		if (retryValue > (MaxRetryValue - digit) / 10) {
			retryValue = MaxRetryValue;
			return;
		}

		retryValue = retryValue * 10 + digit;
	}


	void DecodeByte(uint8 byte) {
		while (true) {
			if (utfNeeded == 0) {
				StartSequence(byte);
				return;
			}

			if (byte < utfLower || byte > utfUpper) {
				ResetSequence();
				EmitReplacement();
				continue;
			}

			utfLower = 0x80;
			utfUpper = 0xBF;
			utfBytes[utfLength] = byte;
			utfLength++;
			if (utfLength == utfNeeded) {
				for (uint8 index = 0; index < utfLength; index++) {
					Emit(utfBytes[index]);
				}

				ResetSequence();
			}

			return;
		}
	}


	void StartSequence(uint8 byte) {
		if (byte <= 0x7F) {
			Emit(byte);
			return;
		}

		if (byte >= 0xC2 && byte <= 0xDF) {
			utfNeeded = 2;
			utfLower = 0x80;
			utfUpper = 0xBF;
		} else if (byte >= 0xE0 && byte <= 0xEF) {
			utfNeeded = 3;
			utfLower = (byte == 0xE0) ? 0xA0 : 0x80;
			utfUpper = (byte == 0xED) ? 0x9F : 0xBF;
		} else if (byte >= 0xF0 && byte <= 0xF4) {
			utfNeeded = 4;
			utfLower = (byte == 0xF0) ? 0x90 : 0x80;
			utfUpper = (byte == 0xF4) ? 0x8F : 0xBF;
		} else {
			EmitReplacement();
			return;
		}

		utfBytes[0] = byte;
		utfLength = 1;
	}


	void ResetSequence() {
		utfNeeded = 0;
		utfLength = 0;
		utfLower = 0x80;
		utfUpper = 0xBF;
	}


	void FlushDecoder() {
		if (utfNeeded != 0) {
			ResetSequence();
			EmitReplacement();
		}
	}


	void EmitReplacement() {
		Emit(0xEF);
		Emit(0xBF);
		Emit(0xBD);
	}


	void Emit(uint8 byte) {
		if (field == Field::Data) {
			AppendData(byte);
			return;
		}

		if (lineOverflow) {
			return;
		}

		ResultStatus status = lineValue.Append(byte, maxEventBytes);
		if (status != ResultStatus::ok) {
			lineOverflow = true;
			Drop(status);
		}
	}


	void AppendData(uint8 byte) {
		if (dropping) {
			return;
		}

		if (data.Size() + eventType.Size() >= maxEventBytes) {
			Drop(ResultStatus::outOfRange);
			return;
		}

		ResultStatus status = data.Append(byte, maxEventBytes);
		if (status != ResultStatus::ok) {
			Drop(status);
		}
	}


	void Drop(ResultStatus reason) {
		dropping = true;
		dropReport = reason;
		data.Clear();
		eventType.Clear();
	}


	void EndLine() {
		FlushDecoder();
		if (!lineHasBytes) {
			Dispatch();
		} else {
			if (!inValue) {
				field = ClassifyName();
			}

			CommitLine();
		}

		ResetLine();
	}


	void CommitLine() {
		switch (field) {
			case Field::Ignored:
				return;
			case Field::Data:
				AppendData('\n');
				return;
			case Field::Event:
				CommitEventType();
				return;
			case Field::Id:
				// An ID that overflowed or held NUL is ignored, and the previous ID stays.
				if (!lineOverflow && !nulSeen) {
					lastEventId.Swap(lineValue);
				}

				return;
			case Field::Retry:
				if (retryValid && retryDigits > 0) {
					retry = retryValue;
					hasRetry = true;
				}

				return;
		}
	}


	void CommitEventType() {
		if (lineOverflow || dropping) {
			return;
		}

		if (lineValue.Size() + data.Size() > maxEventBytes) {
			Drop(ResultStatus::outOfRange);
			return;
		}

		eventType.Swap(lineValue);
	}


	// A blank line ends the event. An event with no data is not dispatched, and neither is one that was dropped.
	void Dispatch() {
		if (dropping || data.Size() == 0) {
			ResetEvent();
			return;
		}

		// Each data line added a LF; the last one is not part of the value.
		data.RemoveLast();
		std::string_view type = (eventType.Size() == 0) ? std::string_view("message") : eventType.View();
		if (onEvent) {
			onEvent(type, data.View(), lastEventId.View());
		}

		ResetEvent();
	}


	void ResetEvent() {
		dropping = false;
		data.Clear();
		eventType.Clear();
	}


	void ResetLine() {
		lineHasBytes = false;
		inValue = false;
		skipSpace = false;
		field = Field::Ignored;
		nameLength = 0;
		lineOverflow = false;
		nulSeen = false;
		retryValid = true;
		retryDigits = 0;
		retryValue = 0;
		lineValue.Clear();
	}


	Field ClassifyName() const {
		if (NameIs("data")) {
			return Field::Data;
		}

		if (NameIs("event")) {
			return Field::Event;
		}

		if (NameIs("id")) {
			return Field::Id;
		}

		if (NameIs("retry")) {
			return Field::Retry;
		}

		return Field::Ignored;
	}


	bool NameIs(std::string_view literal) const {
		if (nameLength != literal.size()) {
			return false;
		}

		return std::string_view(name, nameLength) == literal;
	}
};
