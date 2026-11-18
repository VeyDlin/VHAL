#pragma once
#include <VHAL.h>
#include <array>
#include <span>
#include <string_view>


class Base64 {
private:
	static constexpr std::array<char, 64> encodeTable = {
		'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
		'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
		'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
		'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
	};

	static inline const std::array<uint8, 128> decodeTable = []() {
		std::array<uint8, 128> table{};

		for (size_t i = 0; i < 128; ++i) {
			table[i] = 255;
		}

		for (size_t i = 0; i < 64; ++i) {
			table[static_cast<size_t>(encodeTable[i])] = static_cast<uint8>(i);
		}

		return table;
	}();


public:
	static size_t GetEncodedSize(size_t inputSize) {
		return ((inputSize + 2) / 3) * 4;
	}


	static size_t GetDecodedSize(size_t encodedSize) {
		return (encodedSize / 4) * 3;
	}


	// Writes standard Base64 with '=' padding and returns the number of characters written.
	// Returns 0 when data is empty or output is smaller than GetEncodedSize(data.size()); nothing is written then.
	// No NUL terminator is written.
	static size_t Encode(std::span<char> output, std::span<const uint8> data) {
		if (data.empty()) {
			return 0;
		}

		size_t outputSize = GetEncodedSize(data.size());
		if (output.size() < outputSize) {
			return 0;
		}

		size_t outputPos = 0;
		size_t i = 0;

		while (i < data.size()) {
			uint32 triple = static_cast<uint32>(data[i]) << 16;
			if (i + 1 < data.size()) {
				triple |= static_cast<uint32>(data[i + 1]) << 8;
			}
			if (i + 2 < data.size()) {
				triple |= static_cast<uint32>(data[i + 2]);
			}

			output[outputPos++] = encodeTable[(triple >> 18) & 0x3F];
			output[outputPos++] = encodeTable[(triple >> 12) & 0x3F];
			if (i + 1 < data.size()) {
				output[outputPos++] = encodeTable[(triple >> 6) & 0x3F];
			} else {
				output[outputPos++] = '=';
			}
			if (i + 2 < data.size()) {
				output[outputPos++] = encodeTable[triple & 0x3F];
			} else {
				output[outputPos++] = '=';
			}

			i += 3;
		}

		return outputPos;
	}


	static size_t Encode(std::span<char> output, std::string_view str) {
		return Encode(output, std::span(reinterpret_cast<const uint8*>(str.data()), str.size()));
	}


	// Decodes standard Base64. ASCII whitespace (space, tab, CR, LF) anywhere in the input is ignored.
	// Returns false, with outputSize set to 0 and nothing written to output, when the input has an invalid
	// character, a length that is not a multiple of 4 (ignoring whitespace), misplaced or excess padding,
	// or when output is smaller than the decoded size. Trailing bits of the last group are not checked.
	static bool Decode(std::string_view encoded, std::span<uint8> output, size_t& outputSize) {
		outputSize = 0;

		// First pass validates everything, so a failing call never leaves partial output behind.
		size_t significant = 0;
		size_t paddingCount = 0;
		size_t firstPadding = 0;
		for (char c : encoded) {
			if (IsWhitespace(c)) {
				continue;
			}
			if (c == '=') {
				if (paddingCount == 0) {
					firstPadding = significant;
				}
				++paddingCount;
			} else if (!IsAlphabetChar(c)) {
				return false;
			}
			++significant;
		}

		if (significant % 4 != 0 || paddingCount > 2) {
			return false;
		}
		// Padding must be exactly the final paddingCount characters.
		if (paddingCount > 0 && firstPadding != significant - paddingCount) {
			return false;
		}

		size_t requiredSize = (significant / 4) * 3 - paddingCount;
		if (output.size() < requiredSize) {
			return false;
		}

		// Second pass decodes the already validated input, four characters at a time.
		size_t quarterCount = significant / 4;
		size_t quarterIndex = 0;
		size_t fill = 0;
		std::array<uint8, 4> quarter{};
		size_t written = 0;

		for (char c : encoded) {
			if (IsWhitespace(c)) {
				continue;
			}

			uint8 value = 0;
			if (c != '=') {
				value = decodeTable[static_cast<size_t>(c)];
			}
			quarter[fill++] = value;
			if (fill < 4) {
				continue;
			}

			uint32 combined = (static_cast<uint32>(quarter[0]) << 18) |
				(static_cast<uint32>(quarter[1]) << 12) |
				(static_cast<uint32>(quarter[2]) << 6) |
				static_cast<uint32>(quarter[3]);

			std::array<uint8, 3> bytes = {
				static_cast<uint8>((combined >> 16) & 0xFF),
				static_cast<uint8>((combined >> 8) & 0xFF),
				static_cast<uint8>(combined & 0xFF)
			};

			size_t byteCount = 3;
			if (quarterIndex + 1 == quarterCount) {
				byteCount = 3 - paddingCount;
			}
			for (size_t i = 0; i < byteCount; ++i) {
				output[written++] = bytes[i];
			}

			++quarterIndex;
			fill = 0;
		}

		outputSize = written;
		return true;
	}


	// Decodes into a char buffer, for text payloads. Same rules as Decode: fails when output is too small.
	static bool DecodeToBuffer(std::string_view encoded, std::span<char> output, size_t& outputSize) {
		std::span<uint8> bytes(reinterpret_cast<uint8*>(output.data()), output.size());
		return Decode(encoded, bytes, outputSize);
	}


	// Fixed-size array convenience wrapper around Decode.
	template<size_t MaxSize = 1024>
	static bool DecodeToArray(std::string_view encoded, std::array<uint8, MaxSize>& output, size_t& outputSize) {
		return Decode(encoded, std::span(output), outputSize);
	}

private:
	static constexpr bool IsWhitespace(char c) {
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}


	static constexpr bool IsAlphabetChar(char c) {
		return (c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') ||
			c == '+' || c == '/';
	}
};
