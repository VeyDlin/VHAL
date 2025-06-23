#pragma once
#include <System/System.h>
#include <array>
#include <span>
#include <string_view>

/*
Base64 encoding/decoding implementation
Converts binary data to text format and back

Basic encoding:
    const char* text = "Hello World";
    std::array<char, 64> encoded{};
    size_t len = Base64::Encode(std::span(encoded), text);

Binary data encoding:
    std::array<uint8, 100> binaryData = {...};
    std::array<char, 200> encoded{};
    size_t len = Base64::Encode(std::span(encoded), std::span(binaryData));

Basic decoding:
    const char* base64Text = "SGVsbG8gV29ybGQ=";
    std::array<uint8, 100> decoded{};
    size_t decodedLen;
    bool success = Base64::Decode(base64Text, std::span(decoded), decodedLen);

Decode to char buffer:
    const char* base64Text = "SGVsbG8gV29ybGQ=";
    std::array<char, 100> textBuffer{};
    size_t textLen;
    bool success = Base64::DecodeToBuffer(base64Text, std::span(textBuffer), textLen);

Get required buffer sizes:
    size_t inputSize = 10;
    size_t encodedSize = Base64::GetEncodedSize(inputSize);  // Returns 16
    size_t decodedSize = Base64::GetDecodedSize(encodedSize);  // Returns ~10

JWT header encoding:
    const char* header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    std::array<char, 100> encoded{};
    size_t len = Base64::Encode(std::span(encoded), header);

Image data transmission:
    std::array<uint8, 5000> imageData = {...};
    std::array<char, 10000> base64Image{};
    size_t len = Base64::Encode(std::span(base64Image), std::span(imageData));
*/

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

        // Fill with invalid value
        for (size_t i = 0; i < 128; ++i) {
            table[i] = 255;
        }

        // Fill valid characters
        for (size_t i = 0; i < 64; ++i) {
            table[encodeTable[i]] = static_cast<uint8>(i);
        }

        return table;
    }();

    static constexpr bool IsValidBase64Char(char c) {
        return (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') ||
               c == '+' || c == '/' || c == '=';
    }

public:
    static size_t GetEncodedSize(size_t inputSize) {
        return ((inputSize + 2) / 3) * 4;
    }

    static size_t GetDecodedSize(size_t encodedSize) {
        return (encodedSize / 4) * 3;
    }

    static size_t Encode(std::span<char> output, std::span<const uint8> data) {
        if (data.empty()) {
            return 0;
        }

        size_t outputSize = GetEncodedSize(data.size());
        if (output.size() < outputSize) {
            return 0; // Output buffer too small
        }

        size_t outputPos = 0;
        size_t i = 0;

        while (i < data.size()) {
            uint32 triple = 0;

            // Pack 3 bytes into 24-bit value
            triple = (static_cast<uint32>(data[i]) << 16);
            if (i + 1 < data.size()) {
                triple |= (static_cast<uint32>(data[i + 1]) << 8);
            }
            if (i + 2 < data.size()) {
                triple |= static_cast<uint32>(data[i + 2]);
            }

            // Extract 4 6-bit values
            output[outputPos++] = encodeTable[(triple >> 18) & 0x3F];
            output[outputPos++] = encodeTable[(triple >> 12) & 0x3F];
            output[outputPos++] = (i + 1 < data.size()) ? encodeTable[(triple >> 6) & 0x3F] : '=';
            output[outputPos++] = (i + 2 < data.size()) ? encodeTable[triple & 0x3F] : '=';

            i += 3;
        }

        return outputPos;
    }

    static size_t Encode(std::span<char> output, std::string_view str) {
        return Encode(output, std::span(reinterpret_cast<const uint8*>(str.data()), str.size()));
    }

    static bool Decode(std::string_view encoded, std::span<uint8> output, size_t& outputSize) {
        outputSize = 0;

        if (encoded.empty()) {
            return true;
        }

        // Remove whitespace and validate
        std::array<char, 512> cleanInput{};
        size_t cleanLength = 0;

        for (char c : encoded) {
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                continue; // Skip whitespace
            }
            if (!IsValidBase64Char(c)) {
                return false; // Invalid character
            }
            if (cleanLength < cleanInput.size()) {
                cleanInput[cleanLength++] = c;
            }
        }

        if (cleanLength % 4 != 0) {
            return false; // Invalid length
        }

        size_t paddingCount = 0;
        if (cleanLength >= 2) {
            if (cleanInput[cleanLength - 1] == '=' && cleanInput[cleanLength - 2] == '=') {
                paddingCount = 2;
            } else if (cleanInput[cleanLength - 1] == '=') {
                paddingCount = 1;
            }
        }

        size_t expectedOutputSize = (cleanLength / 4) * 3 - paddingCount;
        if (output.size() < expectedOutputSize) {
            return false; // Output buffer too small
        }

        size_t inputIndex = 0;
        while (inputIndex < cleanLength) {
            std::array<uint8, 4> group{};

            // Decode 4 characters
            for (int i = 0; i < 4; ++i) {
                char c = cleanInput[inputIndex + i];
                if (c == '=') {
                    group[i] = 0;
                } else {
                    if (c >= 128 || decodeTable[c] == 255) {
                        return false; // Invalid character
                    }
                    group[i] = decodeTable[c];
                }
            }

            // Combine into 24-bit value
            uint32 combined = (static_cast<uint32>(group[0]) << 18) |
                             (static_cast<uint32>(group[1]) << 12) |
                             (static_cast<uint32>(group[2]) << 6) |
                             static_cast<uint32>(group[3]);

            // Extract bytes
            if (outputSize < output.size()) {
                output[outputSize++] = static_cast<uint8>((combined >> 16) & 0xFF);
            }
            if (outputSize < output.size() && (inputIndex + 4 < cleanLength || paddingCount < 2)) {
                output[outputSize++] = static_cast<uint8>((combined >> 8) & 0xFF);
            }
            if (outputSize < output.size() && (inputIndex + 4 < cleanLength || paddingCount < 1)) {
                output[outputSize++] = static_cast<uint8>(combined & 0xFF);
            }

            inputIndex += 4;
        }

        // Note: outputSize is already correct - the decode loop handled padding properly
        // Do NOT subtract paddingCount here as it would truncate valid data

        return true;
    }

    // Convenient decode function that returns success/failure
    static bool DecodeToBuffer(std::string_view encoded, std::span<char> output, size_t& outputSize) {
        std::array<uint8, 1024> buffer{};
        size_t binarySize;

        if (!Decode(encoded, std::span(buffer), binarySize)) {
            outputSize = 0;
            return false;
        }

        outputSize = std::min(binarySize, output.size());
        for (size_t i = 0; i < outputSize; ++i) {
            output[i] = static_cast<char>(buffer[i]);
        }

        return true;
    }

    // Convenient decode function that returns a vector-like container
    template<size_t MaxSize = 1024>
    static bool DecodeToArray(std::string_view encoded, std::array<uint8, MaxSize>& output, size_t& outputSize) {
        return Decode(encoded, std::span(output), outputSize);
    }
};

