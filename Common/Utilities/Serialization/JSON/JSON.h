#pragma once
#include <VHAL.h>
#include <Utilities/Console/Print.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>
#include <type_traits>

class JSON {
public:
	enum class Type {
		OBJECT,
		ARRAY,
		STRING,
		NUMBER,
		BOOLEAN,
		NULL_TYPE
	};


	// Writes into a fixed buffer. Commas are not inserted automatically: call Comma() between members.
	class Writer {
	private:
		static constexpr std::array<char, 16> hexDigits = {
			'0', '1', '2', '3', '4', '5', '6', '7',
			'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
		};

		static constexpr std::array<uint64, 10> powersOfTen = {
			1ULL, 10ULL, 100ULL, 1000ULL, 10000ULL,
			100000ULL, 1000000ULL, 10000000ULL, 100000000ULL, 1000000000ULL
		};

		std::span<char> buffer;
		size_t position = 0;
		bool hasError = false;

	public:
		explicit Writer(std::span<char> outputBuffer) : buffer(outputBuffer) {
		}


		Writer& StartObject() {
			WriteChar('{');
			return *this;
		}


		Writer& EndObject() {
			WriteChar('}');
			return *this;
		}


		Writer& StartArray() {
			WriteChar('[');
			return *this;
		}


		Writer& EndArray() {
			WriteChar(']');
			return *this;
		}


		Writer& Key(std::string_view key) {
			WriteChar('"');
			WriteEscapedString(key);
			WriteChar('"');
			WriteChar(':');
			return *this;
		}


		Writer& String(std::string_view value) {
			WriteChar('"');
			WriteEscapedString(value);
			WriteChar('"');
			return *this;
		}


		// Accepts every integer type except bool, so literals such as 16000 are not ambiguous.
		template<typename T>
			requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
		Writer& Number(T value) {
			char numBuffer[32];
			size_t len = Print::NumberToString(numBuffer, sizeof(numBuffer), value, Print::Format::Dec);
			WriteString(std::string_view(numBuffer, len));
			return *this;
		}


		// Up to 9 decimal places (values outside the precision are clamped). NaN, infinity and magnitudes
		// >= 9.0e18 / 10^precision set HasError() because they cannot be written as a plain JSON number.
		Writer& Number(double value, int precision = 6) {
			char numBuffer[32];
			size_t len = FormatFixed(std::span(numBuffer), value, precision);
			if (len == 0) {
				hasError = true;
				return *this;
			}
			WriteString(std::string_view(numBuffer, len));
			return *this;
		}


		Writer& Boolean(bool value) {
			WriteString(value ? "true" : "false");
			return *this;
		}


		Writer& Null() {
			WriteString("null");
			return *this;
		}


		Writer& Comma() {
			WriteChar(',');
			return *this;
		}


		Writer& KeyValue(std::string_view key, std::string_view value) {
			return Key(key).String(value);
		}


		Writer& KeyValue(std::string_view key, const char* value) {
			return Key(key).String(std::string_view(value));
		}


		template<typename T>
			requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
		Writer& KeyValue(std::string_view key, T value) {
			return Key(key).Number(value);
		}


		Writer& KeyValue(std::string_view key, double value, int precision = 6) {
			return Key(key).Number(value, precision);
		}


		Writer& KeyValue(std::string_view key, bool value) {
			return Key(key).Boolean(value);
		}


		size_t GetLength() const {
			return position;
		}


		bool HasError() const {
			return hasError;
		}


		std::string_view GetResult() const {
			return std::string_view(buffer.data(), position);
		}

	private:
		void WriteChar(char c) {
			if (position < buffer.size()) {
				buffer[position++] = c;
			} else {
				hasError = true;
			}
		}


		void WriteString(std::string_view str) {
			for (char c : str) {
				WriteChar(c);
				if (hasError) {
					break;
				}
			}
		}


		void WriteUnicodeEscape(unsigned char c) {
			WriteString("\\u00");
			WriteChar(hexDigits[(c >> 4) & 0x0F]);
			WriteChar(hexDigits[c & 0x0F]);
		}


		void WriteEscapedString(std::string_view str) {
			for (char c : str) {
				switch (c) {
					case '"':
						WriteString("\\\"");
						break;
					case '\\':
						WriteString("\\\\");
						break;
					case '\b':
						WriteString("\\b");
						break;
					case '\f':
						WriteString("\\f");
						break;
					case '\n':
						WriteString("\\n");
						break;
					case '\r':
						WriteString("\\r");
						break;
					case '\t':
						WriteString("\\t");
						break;
					default:
						if (static_cast<unsigned char>(c) < 0x20) {
							WriteUnicodeEscape(static_cast<unsigned char>(c));
						} else {
							WriteChar(c);
						}
						break;
				}
				if (hasError) {
					break;
				}
			}
		}


		// Print::FloatToString truncates instead of rounding (3.14159 -> 3.141589), so the digits are produced here.
		// Returns 0 when the value cannot be represented.
		static size_t FormatFixed(std::span<char> output, double value, int precision) {
			if (!std::isfinite(value)) {
				return 0;
			}

			size_t digits = static_cast<size_t>(std::clamp(precision, 0, 9));
			uint64 scale = powersOfTen[digits];
			double scaled = std::fabs(value) * static_cast<double>(scale);
			if (scaled >= 9.0e18) {
				return 0;
			}

			uint64 rounded = static_cast<uint64>(std::llround(scaled));
			uint64 integerPart = rounded / scale;
			uint64 fractionPart = rounded % scale;

			// Digits are produced least significant first and copied out in reverse.
			char reversed[32];
			size_t count = 0;
			for (size_t i = 0; i < digits; ++i) {
				reversed[count++] = static_cast<char>('0' + fractionPart % 10);
				fractionPart /= 10;
			}
			if (digits > 0) {
				reversed[count++] = '.';
			}
			do {
				reversed[count++] = static_cast<char>('0' + integerPart % 10);
				integerPart /= 10;
			} while (integerPart > 0);
			if (value < 0.0 && rounded != 0) {
				reversed[count++] = '-';
			}

			if (count > output.size()) {
				return 0;
			}
			for (size_t i = 0; i < count; ++i) {
				output[i] = reversed[count - 1 - i];
			}
			return count;
		}
	};


	// Sequential parser over a JSON text. Parsed strings are written into caller-provided buffers.
	// On a failed call the position is unspecified; construct a new Parser to retry.
	class Parser {
	private:
		std::string_view json;
		size_t position = 0;

	public:
		explicit Parser(std::string_view jsonText) : json(jsonText) {
		}


		// Fails (returns false) on invalid escapes, unpaired surrogates, raw control characters,
		// an unterminated string, or when the decoded text does not fit into output.
		bool ParseString(std::span<char> output, size_t& length) {
			SkipWhitespace();
			return DecodeStringAt(json, position, output, length);
		}


		bool ParseNumber(int64& value) {
			SkipWhitespace();
			size_t end = position;
			if (!ScanNumber(json, end)) {
				return false;
			}
			if (!ParseIntegerToken(json.substr(position, end - position), value)) {
				return false;
			}
			position = end;
			return true;
		}


		bool ParseNumber(uint64& value) {
			SkipWhitespace();
			size_t end = position;
			if (!ScanNumber(json, end)) {
				return false;
			}
			if (!ParseIntegerToken(json.substr(position, end - position), value)) {
				return false;
			}
			position = end;
			return true;
		}


		bool ParseNumber(double& value) {
			SkipWhitespace();
			size_t end = position;
			if (!ScanNumber(json, end)) {
				return false;
			}
			if (!ParseFloatToken(json.substr(position, end - position), value)) {
				return false;
			}
			position = end;
			return true;
		}


		bool ParseBoolean(bool& value) {
			SkipWhitespace();
			if (json.substr(position, 4) == "true") {
				value = true;
				position += 4;
				return true;
			}
			if (json.substr(position, 5) == "false") {
				value = false;
				position += 5;
				return true;
			}
			return false;
		}


		bool ParseNull() {
			SkipWhitespace();
			if (json.substr(position, 4) == "null") {
				position += 4;
				return true;
			}
			return false;
		}


		// Looks up a key in the top-level object. Strings are decoded into valueBuffer (empty result on
		// overflow or invalid escapes). Other values (numbers, booleans, null, objects, arrays) are returned
		// as raw text viewing the input. An empty result means the key was not found or could not be read.
		std::string_view FindValue(std::string_view key, std::span<char> valueBuffer) {
			size_t valueStart = 0;
			size_t valueEnd = 0;
			if (!LocateKeyValue(json, key, valueStart, valueEnd)) {
				return std::string_view{};
			}

			if (json[valueStart] == '"') {
				size_t pos = valueStart;
				size_t length = 0;
				if (!DecodeStringAt(json, pos, valueBuffer, length)) {
					return std::string_view{};
				}
				return std::string_view(valueBuffer.data(), length);
			}

			return json.substr(valueStart, valueEnd - valueStart);
		}


		// Advances past the next value of any type. On malformed input the parser moves to the end.
		void SkipValue() {
			size_t end = position;
			if (ScanValue(json, end)) {
				position = end;
			} else {
				position = json.size();
			}
		}


		Type GetValueType() {
			char c = PeekChar();
			if (c == '"') {
				return Type::STRING;
			}
			if (c == '{') {
				return Type::OBJECT;
			}
			if (c == '[') {
				return Type::ARRAY;
			}
			if (c == 't' || c == 'f') {
				return Type::BOOLEAN;
			}
			if (c == '-' || JSON::IsDigit(c)) {
				return Type::NUMBER;
			}
			return Type::NULL_TYPE;
		}


		bool IsAtEnd() {
			SkipWhitespace();
			return position >= json.size();
		}

	private:
		void SkipWhitespace() {
			SkipWhitespaceAt(json, position);
		}


		char PeekChar() {
			SkipWhitespace();
			return position < json.size() ? json[position] : '\0';
		}
	};


	// Reads a string value of the top-level object. Fails if the key is missing, the value is not a string,
	// or the decoded text does not fit into output (length is then 0).
	static bool GetString(std::string_view json, std::string_view key, std::span<char> output, size_t& length) {
		length = 0;

		size_t valueStart = 0;
		size_t valueEnd = 0;
		if (!LocateKeyValue(json, key, valueStart, valueEnd)) {
			return false;
		}
		if (json[valueStart] != '"') {
			return false;
		}

		size_t pos = valueStart;
		return DecodeStringAt(json, pos, output, length);
	}


	static bool GetNumber(std::string_view json, std::string_view key, int64& value) {
		size_t valueStart = 0;
		size_t valueEnd = 0;
		if (!LocateKeyValue(json, key, valueStart, valueEnd)) {
			return false;
		}
		return ParseIntegerToken(json.substr(valueStart, valueEnd - valueStart), value);
	}


	static bool GetNumber(std::string_view json, std::string_view key, uint64& value) {
		size_t valueStart = 0;
		size_t valueEnd = 0;
		if (!LocateKeyValue(json, key, valueStart, valueEnd)) {
			return false;
		}
		return ParseIntegerToken(json.substr(valueStart, valueEnd - valueStart), value);
	}


	static bool GetNumber(std::string_view json, std::string_view key, double& value) {
		size_t valueStart = 0;
		size_t valueEnd = 0;
		if (!LocateKeyValue(json, key, valueStart, valueEnd)) {
			return false;
		}
		return ParseFloatToken(json.substr(valueStart, valueEnd - valueStart), value);
	}


	static bool GetBool(std::string_view json, std::string_view key, bool& value) {
		size_t valueStart = 0;
		size_t valueEnd = 0;
		if (!LocateKeyValue(json, key, valueStart, valueEnd)) {
			return false;
		}

		std::string_view token = json.substr(valueStart, valueEnd - valueStart);
		if (token == "true") {
			value = true;
			return true;
		}
		if (token == "false") {
			value = false;
			return true;
		}
		return false;
	}


	// Returns the raw JSON text of the top-level value for key: an object, array, string (with quotes),
	// number, boolean or null. The result views the input. Empty when the key is missing or malformed.
	static std::string_view GetRaw(std::string_view json, std::string_view key) {
		size_t valueStart = 0;
		size_t valueEnd = 0;
		if (!LocateKeyValue(json, key, valueStart, valueEnd)) {
			return std::string_view{};
		}
		return json.substr(valueStart, valueEnd - valueStart);
	}


	// Returns the raw text of element 'index' of a JSON array (as produced by GetRaw). Empty when the
	// index is out of range or the text is not a well-formed array.
	static std::string_view GetArrayElement(std::string_view array, size_t index) {
		size_t end = 0;
		if (!ScanValue(array, end)) {
			return std::string_view{};
		}

		size_t pos = 0;
		SkipWhitespaceAt(array, pos);
		if (pos >= array.size() || array[pos] != '[') {
			return std::string_view{};
		}
		++pos;

		size_t current = 0;
		while (true) {
			SkipWhitespaceAt(array, pos);
			if (pos < array.size() && array[pos] == ']' && current == 0) {
				return std::string_view{};
			}

			size_t elementStart = pos;
			if (!ScanValue(array, pos)) {
				return std::string_view{};
			}
			if (current == index) {
				return array.substr(elementStart, pos - elementStart);
			}
			++current;

			SkipWhitespaceAt(array, pos);
			if (pos >= array.size() || array[pos] != ',') {
				return std::string_view{};
			}
			++pos;
		}
	}

private:
	static bool IsDigit(char c) {
		return c >= '0' && c <= '9';
	}


	static bool IsWhitespaceChar(char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\r';
	}


	static bool IsDelimiter(char c) {
		return IsWhitespaceChar(c) || c == ',' || c == ':' || c == '}' || c == ']';
	}


	static void SkipWhitespaceAt(std::string_view text, size_t& pos) {
		while (pos < text.size() && IsWhitespaceChar(text[pos])) {
			++pos;
		}
	}


	static bool ReadHex4(std::string_view text, size_t pos, uint32& value) {
		if (pos + 4 > text.size()) {
			return false;
		}

		value = 0;
		for (size_t i = 0; i < 4; ++i) {
			char c = text[pos + i];
			uint32 digit = 0;
			if (c >= '0' && c <= '9') {
				digit = static_cast<uint32>(c - '0');
			} else if (c >= 'a' && c <= 'f') {
				digit = static_cast<uint32>(c - 'a' + 10);
			} else if (c >= 'A' && c <= 'F') {
				digit = static_cast<uint32>(c - 'A' + 10);
			} else {
				return false;
			}
			value = (value << 4) | digit;
		}
		return true;
	}


	static bool MapSimpleEscape(char escaped, char& decoded) {
		switch (escaped) {
			case '"':
				decoded = '"';
				return true;
			case '\\':
				decoded = '\\';
				return true;
			case '/':
				decoded = '/';
				return true;
			case 'b':
				decoded = '\b';
				return true;
			case 'f':
				decoded = '\f';
				return true;
			case 'n':
				decoded = '\n';
				return true;
			case 'r':
				decoded = '\r';
				return true;
			case 't':
				decoded = '\t';
				return true;
			default:
				return false;
		}
	}


	static void EncodeUtf8(uint32 codePoint, std::array<char, 4>& bytes, size_t& byteCount) {
		if (codePoint < 0x80) {
			bytes[0] = static_cast<char>(codePoint);
			byteCount = 1;
		} else if (codePoint < 0x800) {
			bytes[0] = static_cast<char>(0xC0 | (codePoint >> 6));
			bytes[1] = static_cast<char>(0x80 | (codePoint & 0x3F));
			byteCount = 2;
		} else if (codePoint < 0x10000) {
			bytes[0] = static_cast<char>(0xE0 | (codePoint >> 12));
			bytes[1] = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
			bytes[2] = static_cast<char>(0x80 | (codePoint & 0x3F));
			byteCount = 3;
		} else {
			bytes[0] = static_cast<char>(0xF0 | (codePoint >> 18));
			bytes[1] = static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
			bytes[2] = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
			bytes[3] = static_cast<char>(0x80 | (codePoint & 0x3F));
			byteCount = 4;
		}
	}


	// pos points at the first hex digit after "\u". Surrogate pairs are combined into one code point.
	static bool DecodeUnicodeEscape(std::string_view text, size_t& pos, std::array<char, 4>& bytes, size_t& byteCount) {
		uint32 codePoint = 0;
		if (!ReadHex4(text, pos, codePoint)) {
			return false;
		}
		pos += 4;

		if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
			return false;
		}

		if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
			uint32 lowSurrogate = 0;
			if (pos + 2 > text.size() || text[pos] != '\\' || text[pos + 1] != 'u') {
				return false;
			}
			if (!ReadHex4(text, pos + 2, lowSurrogate)) {
				return false;
			}
			if (lowSurrogate < 0xDC00 || lowSurrogate > 0xDFFF) {
				return false;
			}
			pos += 6;
			codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (lowSurrogate - 0xDC00);
		}

		EncodeUtf8(codePoint, bytes, byteCount);
		return true;
	}


	// Decodes one logical character of a string body starting at text[pos]. Returns false on a raw control
	// character, an invalid escape, or a truncated escape. The closing quote is never consumed here.
	static bool DecodeUnit(std::string_view text, size_t& pos, std::array<char, 4>& bytes, size_t& byteCount) {
		byteCount = 0;
		if (pos >= text.size()) {
			return false;
		}

		char c = text[pos];
		if (static_cast<unsigned char>(c) < 0x20) {
			return false;
		}

		if (c != '\\') {
			bytes[0] = c;
			byteCount = 1;
			++pos;
			return true;
		}

		if (pos + 1 >= text.size()) {
			return false;
		}
		char escaped = text[pos + 1];
		pos += 2;

		if (escaped == 'u') {
			return DecodeUnicodeEscape(text, pos, bytes, byteCount);
		}

		char decoded = '\0';
		if (!MapSimpleEscape(escaped, decoded)) {
			return false;
		}
		bytes[0] = decoded;
		byteCount = 1;
		return true;
	}


	// pos points at the opening quote. On success pos is moved past the closing quote and the decoded text
	// is written to output. On failure length is 0.
	static bool DecodeStringAt(std::string_view text, size_t& pos, std::span<char> output, size_t& length) {
		length = 0;
		if (pos >= text.size() || text[pos] != '"') {
			return false;
		}
		++pos;

		while (pos < text.size() && text[pos] != '"') {
			std::array<char, 4> bytes{};
			size_t byteCount = 0;
			if (!DecodeUnit(text, pos, bytes, byteCount)) {
				length = 0;
				return false;
			}
			if (length + byteCount > output.size()) {
				length = 0;
				return false;
			}
			for (size_t i = 0; i < byteCount; ++i) {
				output[length++] = bytes[i];
			}
		}

		if (pos >= text.size()) {
			length = 0;
			return false;
		}
		++pos;
		return true;
	}


	// Validates a string token without producing output.
	static bool ScanString(std::string_view text, size_t& pos) {
		if (pos >= text.size() || text[pos] != '"') {
			return false;
		}
		++pos;

		while (pos < text.size() && text[pos] != '"') {
			std::array<char, 4> bytes{};
			size_t byteCount = 0;
			if (!DecodeUnit(text, pos, bytes, byteCount)) {
				return false;
			}
		}

		if (pos >= text.size()) {
			return false;
		}
		++pos;
		return true;
	}


	// Compares a validated key token (starting at keyStart) with key without allocating.
	static bool KeyEquals(std::string_view text, size_t keyStart, std::string_view key) {
		size_t pos = keyStart + 1;
		size_t matched = 0;

		while (pos < text.size() && text[pos] != '"') {
			std::array<char, 4> bytes{};
			size_t byteCount = 0;
			if (!DecodeUnit(text, pos, bytes, byteCount)) {
				return false;
			}
			for (size_t i = 0; i < byteCount; ++i) {
				if (matched >= key.size() || key[matched] != bytes[i]) {
					return false;
				}
				++matched;
			}
		}
		return matched == key.size();
	}


	// Matches the JSON number grammar exactly: no leading '+', no leading zeros, digits required after '.' and 'e'.
	static bool ScanNumber(std::string_view text, size_t& pos) {
		size_t p = pos;
		if (p < text.size() && text[p] == '-') {
			++p;
		}
		if (p >= text.size() || !IsDigit(text[p])) {
			return false;
		}

		if (text[p] == '0') {
			++p;
			if (p < text.size() && IsDigit(text[p])) {
				return false;
			}
		} else {
			while (p < text.size() && IsDigit(text[p])) {
				++p;
			}
		}

		if (p < text.size() && text[p] == '.') {
			++p;
			if (p >= text.size() || !IsDigit(text[p])) {
				return false;
			}
			while (p < text.size() && IsDigit(text[p])) {
				++p;
			}
		}

		if (p < text.size() && (text[p] == 'e' || text[p] == 'E')) {
			++p;
			if (p < text.size() && (text[p] == '+' || text[p] == '-')) {
				++p;
			}
			if (p >= text.size() || !IsDigit(text[p])) {
				return false;
			}
			while (p < text.size() && IsDigit(text[p])) {
				++p;
			}
		}

		pos = p;
		return true;
	}


	// Skips an object or array by bracket depth, without recursion, so arbitrarily deep input cannot exhaust
	// the stack. Strings and bare tokens (numbers, literals) are validated; ':' and ',' placement and the
	// matching of '{' with ']' are not checked.
	static bool ScanComposite(std::string_view text, size_t& pos) {
		size_t depth = 0;
		while (pos < text.size()) {
			char c = text[pos];
			if (c == '"') {
				if (!ScanString(text, pos)) {
					return false;
				}
			} else if (c == '{' || c == '[') {
				++depth;
				++pos;
			} else if (c == '}' || c == ']') {
				if (depth == 0) {
					return false;
				}
				--depth;
				++pos;
				if (depth == 0) {
					return true;
				}
			} else if (IsWhitespaceChar(c) || c == ':' || c == ',') {
				++pos;
			} else if (!ScanBareToken(text, pos)) {
				return false;
			}
		}
		return false;
	}


	// A number or literal (true, false, null). It must be followed by a delimiter or the end of the text.
	static bool ScanBareToken(std::string_view text, size_t& pos) {
		if (pos >= text.size()) {
			return false;
		}

		char c = text[pos];
		if (c == '-' || IsDigit(c)) {
			if (!ScanNumber(text, pos)) {
				return false;
			}
		} else if (text.substr(pos, 4) == "true") {
			pos += 4;
		} else if (text.substr(pos, 5) == "false") {
			pos += 5;
		} else if (text.substr(pos, 4) == "null") {
			pos += 4;
		} else {
			return false;
		}

		return pos >= text.size() || IsDelimiter(text[pos]);
	}


	// Advances pos past one complete value of any type, skipping leading whitespace.
	static bool ScanValue(std::string_view text, size_t& pos) {
		SkipWhitespaceAt(text, pos);
		if (pos >= text.size()) {
			return false;
		}

		char c = text[pos];
		if (c == '"') {
			return ScanString(text, pos);
		}
		if (c == '{' || c == '[') {
			return ScanComposite(text, pos);
		}
		return ScanBareToken(text, pos);
	}


	// Finds the first occurrence of key among the top-level members of the object text.
	// valueStart/valueEnd delimit the raw value text inside text. The whole object must be closed:
	// a truncated object yields no members, so a partial payload cannot be mistaken for a complete one.
	static bool LocateKeyValue(std::string_view text, std::string_view key, size_t& valueStart, size_t& valueEnd) {
		size_t end = 0;
		if (!ScanValue(text, end)) {
			return false;
		}

		size_t pos = 0;
		SkipWhitespaceAt(text, pos);
		if (pos >= text.size() || text[pos] != '{') {
			return false;
		}
		++pos;

		while (true) {
			SkipWhitespaceAt(text, pos);
			if (pos >= text.size() || text[pos] == '}') {
				return false;
			}

			size_t keyStart = pos;
			if (!ScanString(text, pos)) {
				return false;
			}
			bool isMatch = KeyEquals(text, keyStart, key);

			SkipWhitespaceAt(text, pos);
			if (pos >= text.size() || text[pos] != ':') {
				return false;
			}
			++pos;
			SkipWhitespaceAt(text, pos);

			size_t valueBegin = pos;
			if (!ScanValue(text, pos)) {
				return false;
			}
			if (isMatch) {
				valueStart = valueBegin;
				valueEnd = pos;
				return true;
			}

			SkipWhitespaceAt(text, pos);
			if (pos >= text.size() || text[pos] != ',') {
				return false;
			}
			++pos;
		}
	}


	static bool IsNumberToken(std::string_view token) {
		size_t pos = 0;
		return ScanNumber(token, pos) && pos == token.size();
	}


	static bool IsIntegerToken(std::string_view token) {
		return IsNumberToken(token) && token.find_first_of(".eE") == std::string_view::npos;
	}


	// The token is copied into a bounded, NUL-terminated buffer first: Print's converters read until a NUL.
	template<typename T>
	static bool ParseIntegerToken(std::string_view token, T& value) {
		if (!IsIntegerToken(token)) {
			return false;
		}

		std::array<char, 32> buffer{};
		if (token.size() >= buffer.size()) {
			return false;
		}
		std::copy(token.begin(), token.end(), buffer.begin());
		return Print::StringToNumber(buffer.data(), value);
	}


	static bool ParseFloatToken(std::string_view token, double& value) {
		if (!IsNumberToken(token)) {
			return false;
		}

		std::array<char, 64> buffer{};
		if (token.size() >= buffer.size()) {
			return false;
		}
		std::copy(token.begin(), token.end(), buffer.begin());

		// Magnitudes beyond the double range convert to infinity, which JSON cannot represent.
		double parsed = 0.0;
		if (!Print::StringToFloat(buffer.data(), parsed) || !std::isfinite(parsed)) {
			return false;
		}
		value = parsed;
		return true;
	}
};
