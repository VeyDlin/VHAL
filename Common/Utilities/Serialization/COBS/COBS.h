#pragma once
#include <VHAL.h>
#include <cstdint>
#include <cstddef>
#include <stdexcept>


template <size_t TempBufferSize = 256>
class COBS {
public:
    struct Config {
        uint8 startByte = 0xFF;
        uint8 stopByte = 0x00;
        uint8 escapeByte = 0xFE;
        uint8 escapedStart = 0x01;
        uint8 escapedStop = 0x02;
        uint8 escapedEscape = 0x03;
    };

private:
    const Config config;

public:
    COBS(const Config& config) : config(config) {
        SystemAssert(config.startByte != config.stopByte);
        SystemAssert(config.startByte != config.escapeByte);
        SystemAssert(config.stopByte != config.escapeByte);
    }

    Config GetConfig() const {
        return config;
    }

    Result<size_t> Encode(const uint8* data, size_t length, uint8* output, size_t maxOutputLength) const {
        uint8 tempBuffer[TempBufferSize];
        if (length > TempBufferSize) {
            return ResultStatus::bufferOverflow;
        }

        auto escapedLength = Escape(data, length, tempBuffer, TempBufferSize);
        if (escapedLength.IsErr()) {
            return escapedLength.Error();
        }

        size_t readIndex = 0;
        size_t writeIndex = 2;
        size_t blockStart = 1;

        // Start byte, stop byte, one block code per block, and one extra block code after every 254 data bytes.
        size_t requiredLength = escapedLength.Value() + escapedLength.Value() / 254 + 3;
        if (requiredLength > maxOutputLength) {
            return ResultStatus::bufferOverflow;
        }

        output[0] = config.startByte;
        output[1] = 0; // placeholder for block length

        while (readIndex < escapedLength.Value()) {
            if (tempBuffer[readIndex] == 0) {
                output[blockStart] = writeIndex - blockStart;
                blockStart = writeIndex++;
            }
            else {
                output[writeIndex++] = tempBuffer[readIndex];
                // A block code cannot exceed 0xFF; close a full block and start the next one.
                if (writeIndex - blockStart == 0xFF) {
                    output[blockStart] = 0xFF;
                    blockStart = writeIndex++;
                }
            }
            readIndex++;
        }

        output[blockStart] = writeIndex - blockStart;
        output[writeIndex++] = config.stopByte;

        return writeIndex;
    }

    Result<size_t> Decode(const uint8* data, size_t length, uint8* output, size_t maxOutputLength) const {
        if (length < 2 || data[0] != config.startByte || data[length - 1] != config.stopByte) {
            return ResultStatus::invalidArgument;
        }

        uint8 tempBuffer[TempBufferSize];
        if (length - 2 > TempBufferSize) {
            return ResultStatus::bufferOverflow;
        }

        size_t readIndex = 1;
        size_t writeIndex = 0;

        while (readIndex < length - 1) {
            uint8 code = data[readIndex++];
            if (code == 0) {
                return ResultStatus::dataCorrupted;
            }
            for (size_t i = 1; i < code; ++i) {
                if (readIndex >= length - 1) {
                    return ResultStatus::dataCorrupted;
                }
                tempBuffer[writeIndex++] = data[readIndex++];
            }
            if (code < 0xFF && readIndex < length - 1) {
                tempBuffer[writeIndex++] = 0;
            }
        }

        return Unescape(tempBuffer, writeIndex, output, maxOutputLength);
    }

private:
    Result<size_t> Escape(const uint8* input, size_t length, uint8* output, size_t maxOutputLength) const {
        size_t writeIndex = 0;

        for (size_t i = 0; i < length; ++i) {
            if (input[i] == config.startByte) {
                if (writeIndex + 2 > maxOutputLength) return ResultStatus::bufferOverflow;
                output[writeIndex++] = config.escapeByte;
                output[writeIndex++] = config.escapedStart;
            }
            else if (input[i] == config.stopByte) {
                if (writeIndex + 2 > maxOutputLength) return ResultStatus::bufferOverflow;
                output[writeIndex++] = config.escapeByte;
                output[writeIndex++] = config.escapedStop;
            }
            else if (input[i] == config.escapeByte) {
                if (writeIndex + 2 > maxOutputLength) return ResultStatus::bufferOverflow;
                output[writeIndex++] = config.escapeByte;
                output[writeIndex++] = config.escapedEscape;
            }
            else {
                if (writeIndex + 1 > maxOutputLength) return ResultStatus::bufferOverflow;
                output[writeIndex++] = input[i];
            }
        }

        return writeIndex;
    }

    Result<size_t> Unescape(const uint8* input, size_t length, uint8* output, size_t maxOutputLength) const {
        size_t writeIndex = 0;

        for (size_t i = 0; i < length; ++i) {
            if (input[i] == config.escapeByte) {
                ++i;
                if (i >= length) return ResultStatus::dataCorrupted;
                if (input[i] == config.escapedStart) {
                    if (writeIndex + 1 > maxOutputLength) return ResultStatus::bufferOverflow;
                    output[writeIndex++] = config.startByte;
                }
                else if (input[i] == config.escapedStop) {
                    if (writeIndex + 1 > maxOutputLength) return ResultStatus::bufferOverflow;
                    output[writeIndex++] = config.stopByte;
                }
                else if (input[i] == config.escapedEscape) {
                    if (writeIndex + 1 > maxOutputLength) return ResultStatus::bufferOverflow;
                    output[writeIndex++] = config.escapeByte;
                }
                else {
                    return ResultStatus::dataCorrupted;
                }
            }
            else {
                if (writeIndex + 1 > maxOutputLength) return ResultStatus::bufferOverflow;
                output[writeIndex++] = input[i];
            }
        }

        return writeIndex;
    }
};
