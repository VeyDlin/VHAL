#pragma once
#include <System/System.h>
#include <array>
#include <span>
#include <string_view>
#include <algorithm>
#include <Utilities/Print.h>

/*
JSON serialization and parsing for embedded systems
Supports reading/writing JSON without dynamic memory allocation

JSON Writing:
    std::array<char, 256> buffer{};
    JSON::Writer writer(std::span(buffer));
    writer.StartObject()
          .KeyValue("name", "John")
          .Comma()
          .KeyValue("age", 30)
          .Comma()
          .KeyValue("active", true)
          .EndObject();

Complex objects:
    JSON::Writer writer(buffer);
    writer.StartObject()
          .Key("user").StartObject()
              .KeyValue("id", 123)
              .Comma()
              .KeyValue("email", "john@example.com")
          .EndObject()
          .Comma()
          .Key("settings").StartArray()
              .String("dark_mode")
              .Comma()
              .String("notifications")
          .EndArray()
          .EndObject();

JSON Parsing:
    const char* json = "{\"temperature\":25.5,\"humidity\":60}";
    double temp;
    uint32 humidity;
    bool hasTemp = JSON::GetNumber(json, "temperature", temp);
    bool hasHumidity = JSON::GetNumber(json, "humidity", humidity);

String extraction:
    const char* json = "{\"device\":\"sensor01\",\"location\":\"room1\"}";
    std::array<char, 32> deviceBuffer{};
    size_t deviceLen;
    bool found = JSON::GetString(json, "device", std::span(deviceBuffer), deviceLen);

Boolean values:
    const char* json = "{\"enabled\":true,\"debug\":false}";
    bool enabled = JSON::GetBoolean(json, "enabled");  // Returns true
    bool debug = JSON::GetBoolean(json, "debug");      // Returns false

API response parsing:
    const char* response = "{\"status\":\"ok\",\"code\":200,\"data\":{\"count\":5}}";
    std::array<char, 16> statusBuffer{};
    size_t statusLen;
    uint32 code;
    JSON::GetString(response, "status", std::span(statusBuffer), statusLen);
    JSON::GetNumber(response, "code", code);

Device configuration:
    JSON::Writer config(buffer);
    config.StartObject()
          .KeyValue("device_id", deviceId)
          .Comma()
          .KeyValue("sample_rate", 1000)
          .Comma()
          .KeyValue("enabled", true)
          .EndObject();
*/

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

    // JSON Writer for streaming output to buffer
    class Writer {
    private:
        std::span<char> buffer;
        size_t position = 0;
        bool hasError = false;

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
                if (hasError) break;
            }
        }

        void WriteEscapedString(std::string_view str) {
            for (char c : str) {
                switch (c) {
                    case '"': WriteString("\\\""); break;
                    case '\\': WriteString("\\\\"); break;
                    case '\b': WriteString("\\b"); break;
                    case '\f': WriteString("\\f"); break;
                    case '\n': WriteString("\\n"); break;
                    case '\r': WriteString("\\r"); break;
                    case '\t': WriteString("\\t"); break;
                    default: WriteChar(c); break;
                }
                if (hasError) break;
            }
        }

    public:
        explicit Writer(std::span<char> outputBuffer) : buffer(outputBuffer) {}

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

        Writer& Number(int64 value) {
            char numBuffer[32];
            size_t len = Print::NumberToString(numBuffer, sizeof(numBuffer), value, Print::Format::Dec);
            WriteString(std::string_view(numBuffer, len));
            return *this;
        }

        Writer& Number(uint64 value) {
            char numBuffer[32];
            size_t len = Print::NumberToString(numBuffer, sizeof(numBuffer), value, Print::Format::Dec);
            WriteString(std::string_view(numBuffer, len));
            return *this;
        }

        Writer& Number(double value, int precision = 6) {
            char numBuffer[64];
            size_t len = Print::FloatToString(numBuffer, sizeof(numBuffer), value, static_cast<uint8>(precision));
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

        Writer& KeyValue(std::string_view key, int64 value) {
            return Key(key).Number(value);
        }

        Writer& KeyValue(std::string_view key, uint64 value) {
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
    };

    // JSON Parser for reading from buffer
    class Parser {
    private:
        std::string_view json;
        size_t position = 0;

        void SkipWhitespace() {
            while (position < json.size() &&
                   (json[position] == ' ' || json[position] == '\t' ||
                    json[position] == '\n' || json[position] == '\r')) {
                position++;
            }
        }

        char PeekChar() {
            SkipWhitespace();
            return position < json.size() ? json[position] : '\0';
        }

        char NextChar() {
            SkipWhitespace();
            return position < json.size() ? json[position++] : '\0';
        }

        bool Match(char expected) {
            if (PeekChar() == expected) {
                position++;
                return true;
            }
            return false;
        }

    public:
        explicit Parser(std::string_view jsonText) : json(jsonText) {}

        bool ParseString(std::span<char> output, size_t& length) {
            length = 0;

            if (!Match('"')) return false;

            while (position < json.size() && json[position] != '"') {
                char c = json[position++];

                if (c == '\\' && position < json.size()) {
                    char escaped = json[position++];
                    switch (escaped) {
                        case '"': c = '"'; break;
                        case '\\': c = '\\'; break;
                        case '/': c = '/'; break;
                        case 'b': c = '\b'; break;
                        case 'f': c = '\f'; break;
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        default: c = escaped; break;
                    }
                }

                if (length < output.size()) {
                    output[length++] = c;
                }
            }

            return Match('"');
        }

        bool ParseNumber(int64& value) {
            SkipWhitespace();
            
            size_t endPos;
            bool success = Print::StringToNumber(&json[position], value, &endPos);
            if (success) {
                position += endPos;
            }
            return success;
        }

        bool ParseNumber(uint64& value) {
            SkipWhitespace();
            
            size_t endPos;
            bool success = Print::StringToNumber(&json[position], value, &endPos);
            if (success) {
                position += endPos;
            }
            return success;
        }

        bool ParseNumber(double& value) {
            SkipWhitespace();
            
            size_t endPos;
            bool success = Print::StringToFloat(&json[position], value, &endPos);
            if (success) {
                position += endPos;
            }
            return success;
        }
        
        bool ParseNumberOld(double& value) {
            // Old implementation kept for reference
            SkipWhitespace();

            size_t start = position;

            if (position >= json.size()) return false;

            // Manual double parsing to avoid strtod
            bool negative = false;
            if (json[position] == '-') {
                negative = true;
                position++;
            }

            if (position >= json.size() || json[position] < '0' || json[position] > '9') {
                position = start;
                return false;
            }

            // Parse integer part
            double result = 0;
            while (position < json.size() && json[position] >= '0' && json[position] <= '9') {
                result = result * 10 + (json[position] - '0');
                position++;
            }

            // Parse fractional part
            if (position < json.size() && json[position] == '.') {
                position++;
                double fraction = 0;
                double divisor = 10;

                while (position < json.size() && json[position] >= '0' && json[position] <= '9') {
                    fraction += (json[position] - '0') / divisor;
                    divisor *= 10;
                    position++;
                }

                result += fraction;
            }

            // Parse exponent (simplified)
            if (position < json.size() && (json[position] == 'e' || json[position] == 'E')) {
                position++;
                bool expNegative = false;
                if (position < json.size() && json[position] == '-') {
                    expNegative = true;
                    position++;
                } else if (position < json.size() && json[position] == '+') {
                    position++;
                }

                int exponent = 0;
                while (position < json.size() && json[position] >= '0' && json[position] <= '9') {
                    exponent = exponent * 10 + (json[position] - '0');
                    position++;
                }

                // Apply exponent (simple power of 10)
                for (int i = 0; i < exponent; ++i) {
                    if (expNegative) {
                        result /= 10;
                    } else {
                        result *= 10;
                    }
                }
            }

            value = negative ? -result : result;
            return true;
        }

        bool ParseBoolean(bool& value) {
            if (position + 4 <= json.size() && json.substr(position, 4) == "true") {
                value = true;
                position += 4;
                return true;
            }

            if (position + 5 <= json.size() && json.substr(position, 5) == "false") {
                value = false;
                position += 5;
                return true;
            }

            return false;
        }

        bool ParseNull() {
            if (position + 4 <= json.size() && json.substr(position, 4) == "null") {
                position += 4;
                return true;
            }
            return false;
        }

        std::string_view FindValue(std::string_view key, std::span<char> valueBuffer) {
            size_t savedPosition = position;
            position = 0;

            // Look for key in object
            if (!Match('{')) {
                position = savedPosition;
                return std::string_view{};
            }

            while (PeekChar() != '}' && PeekChar() != '\0') {
                // Parse key
                std::array<char, 64> keyBuffer{};
                size_t keyLength;

                if (!ParseString(std::span(keyBuffer), keyLength)) {
                    position = savedPosition;
                    return std::string_view{};
                }

                if (!Match(':')) {
                    position = savedPosition;
                    return std::string_view{};
                }

                std::string_view foundKey(keyBuffer.data(), keyLength);
                if (foundKey == key) {
                    // Found the key, parse the value
                    char nextChar = PeekChar();

                    if (nextChar == '"') {
                        // String value
                        size_t valueLength;
                        if (ParseString(valueBuffer, valueLength)) {
                            position = savedPosition;
                            return std::string_view(valueBuffer.data(), valueLength);
                        }
                    } else if ((nextChar >= '0' && nextChar <= '9') || nextChar == '-') {
                        // Number value
                        size_t start = position;
                        SkipWhitespace();

                        while (position < json.size() &&
                               ((json[position] >= '0' && json[position] <= '9') ||
                                json[position] == '.' || json[position] == '-' ||
                                json[position] == '+' || json[position] == 'e' || json[position] == 'E')) {
                            position++;
                        }

                        size_t valueLength = std::min(position - start, valueBuffer.size() - 1);
                        for (size_t i = 0; i < valueLength; ++i) {
                            valueBuffer[i] = json[start + i];
                        }
                        valueBuffer[valueLength] = '\0';

                        position = savedPosition;
                        return std::string_view(valueBuffer.data(), valueLength);
                    }

                    position = savedPosition;
                    return std::string_view{};
                } else {
                    // Skip this value
                    SkipValue();
                }

                if (PeekChar() == ',') {
                    NextChar(); // consume comma
                }
            }

            position = savedPosition;
            return std::string_view{};
        }

        void SkipValue() {
            char c = PeekChar();

            if (c == '"') {
                // Skip string
                NextChar(); // consume opening quote
                while (position < json.size() && json[position] != '"') {
                    if (json[position] == '\\') position++; // skip escaped char
                    position++;
                }
                if (position < json.size()) position++; // consume closing quote
            } else if (c == '{') {
                // Skip object
                NextChar(); // consume {
                int depth = 1;
                while (position < json.size() && depth > 0) {
                    char ch = json[position++];
                    if (ch == '"') {
                        // Skip string content
                        while (position < json.size() && json[position] != '"') {
                            if (json[position] == '\\') position++;
                            position++;
                        }
                        if (position < json.size()) position++;
                    } else if (ch == '{') {
                        depth++;
                    } else if (ch == '}') {
                        depth--;
                    }
                }
            } else if (c == '[') {
                // Skip array
                NextChar(); // consume [
                int depth = 1;
                while (position < json.size() && depth > 0) {
                    char ch = json[position++];
                    if (ch == '"') {
                        // Skip string content
                        while (position < json.size() && json[position] != '"') {
                            if (json[position] == '\\') position++;
                            position++;
                        }
                        if (position < json.size()) position++;
                    } else if (ch == '[') {
                        depth++;
                    } else if (ch == ']') {
                        depth--;
                    }
                }
            } else {
                // Skip primitive (number, boolean, null)
                while (position < json.size() &&
                       json[position] != ',' && json[position] != '}' &&
                       json[position] != ']' && json[position] != ' ' &&
                       json[position] != '\t' && json[position] != '\n' &&
                       json[position] != '\r') {
                    position++;
                }
            }
        }

        Type GetValueType() {
            char c = PeekChar();
            if (c == '"') return Type::STRING;
            if (c == '{') return Type::OBJECT;
            if (c == '[') return Type::ARRAY;
            if (c == 't' || c == 'f') return Type::BOOLEAN;
            if (c == 'n') return Type::NULL_TYPE;
            if ((c >= '0' && c <= '9') || c == '-') return Type::NUMBER;
            return Type::NULL_TYPE;
        }

        bool IsAtEnd() {
            return PeekChar() == '\0';
        }
    };

    // Helper functions for quick operations
    static bool GetString(std::string_view json, std::string_view key, std::span<char> output, size_t& length) {
        std::array<char, 128> valueBuffer{};
        Parser parser(json);
        std::string_view value = parser.FindValue(key, std::span(valueBuffer));

        if (value.empty()) {
            length = 0;
            return false;
        }

        length = std::min(value.size(), output.size());
        for (size_t i = 0; i < length; ++i) {
            output[i] = value[i];
        }

        return true;
    }

    static bool GetNumber(std::string_view json, std::string_view key, int64& value) {
        std::array<char, 32> valueBuffer{};
        Parser parser(json);
        std::string_view valueStr = parser.FindValue(key, std::span(valueBuffer));

        if (valueStr.empty()) return false;

        value = strtoll(valueStr.data(), nullptr, 10);
        return true;
    }

    static bool GetNumber(std::string_view json, std::string_view key, uint64& value) {
        std::array<char, 32> valueBuffer{};
        Parser parser(json);
        std::string_view valueStr = parser.FindValue(key, std::span(valueBuffer));

        if (valueStr.empty()) return false;

        value = strtoull(valueStr.data(), nullptr, 10);
        return true;
    }

    static bool GetNumber(std::string_view json, std::string_view key, double& value) {
        std::array<char, 32> valueBuffer{};
        Parser parser(json);
        std::string_view valueStr = parser.FindValue(key, std::span(valueBuffer));

        if (valueStr.empty()) return false;

        // Manual double parsing
        size_t pos = 0;
        bool negative = false;

        if (pos < valueStr.size() && valueStr[pos] == '-') {
            negative = true;
            pos++;
        }

        if (pos >= valueStr.size() || valueStr[pos] < '0' || valueStr[pos] > '9') {
            return false;
        }

        double result = 0;
        while (pos < valueStr.size() && valueStr[pos] >= '0' && valueStr[pos] <= '9') {
            result = result * 10 + (valueStr[pos] - '0');
            pos++;
        }

        if (pos < valueStr.size() && valueStr[pos] == '.') {
            pos++;
            double fraction = 0;
            double divisor = 10;

            while (pos < valueStr.size() && valueStr[pos] >= '0' && valueStr[pos] <= '9') {
                fraction += (valueStr[pos] - '0') / divisor;
                divisor *= 10;
                pos++;
            }

            result += fraction;
        }

        value = negative ? -result : result;
        return true;
    }
};
