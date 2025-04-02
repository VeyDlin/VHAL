#pragma once
#include <System/System.h>
#include <cstdint>
#include <cstddef>
#include <stdexcept>


class COBS {
public:
    static constexpr size_t defaultTempBufferSize = 256;

    struct Config {
        uint8 startByte = 0xFF;     
        uint8 stopByte = 0x00;      
        uint8 escapeByte = 0xFE;    
        uint8 escapedStart = 0x01;  
        uint8 escapedStop = 0x02;   
    };

private:
    const Config config;

public:
    COBS(const Config& config): config(config) {
        SystemAssert(config.startByte != config.stopByte);
        SystemAssert(config.startByte != config.escapeByte);
        SystemAssert(config.stopByte != config.escapeByte);
    }


    Config GetConfig() const {
        return config;
    }   


    Status::info<size_t> Encode(const uint8* data, size_t length, uint8* output, size_t maxOutputLength) const {
        uint8 tempBuffer[defaultTempBufferSize]; 
        if (length > defaultTempBufferSize) {
            return Status::bufferOverflow;
        }

        auto escapedLength = Escape(data, length, tempBuffer, defaultTempBufferSize);
        if(escapedLength.IsError()) {
            return escapedLength.type;
        }

        size_t readIndex = 0;
        size_t writeIndex = 1;
        size_t blockStart = 0;

        if (escapedLength.data + 2 > maxOutputLength) {
            return Status::bufferOverflow;
        }

        output[0] = config.startByte;

        while (readIndex < escapedLength.data) {
            if (tempBuffer[readIndex] == 0) {
                output[blockStart] = writeIndex - blockStart;
                blockStart = writeIndex++;
            } else {
                output[writeIndex++] = tempBuffer[readIndex];
            }
            readIndex++;
        }

        output[blockStart] = writeIndex - blockStart; 
        output[writeIndex++] = config.stopByte; 

        return writeIndex;
    }


    Status::info<size_t> Decode(const uint8* data, size_t length, uint8* output, size_t maxOutputLength) const {
        if (length < 2 || data[0] != config.startByte || data[length - 1] != config.stopByte) {
            return Status::invalidArgument; 
        }

        uint8 tempBuffer[defaultTempBufferSize];
        if (length - 2 > defaultTempBufferSize) {
            return Status::bufferOverflow;
        }

        size_t readIndex = 1;
        size_t writeIndex = 0;

        while (readIndex < length - 1) {
            uint8 code = data[readIndex++];
            for (size_t i = 1; i < code; ++i) {
                if (readIndex >= length - 1) {
                    return Status::dataCorrupted;
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
    Status::info<size_t> Escape(const uint8* input, size_t length, uint8* output, size_t maxOutputLength) const {
        size_t writeIndex = 0;

        for (size_t i = 0; i < length; ++i) {
            if (input[i] == config.startByte) {
                if (writeIndex + 2 > maxOutputLength) {
                    return Status::bufferOverflow;
                }
                output[writeIndex++] = config.escapeByte;
                output[writeIndex++] = config.escapedStart;
            } else if (input[i] == config.stopByte) {
                if (writeIndex + 2 > maxOutputLength) {
                    return Status::bufferOverflow;
                }
                output[writeIndex++] = config.escapeByte;
                output[writeIndex++] = config.escapedStop;
            } else {
                if (writeIndex + 1 > maxOutputLength) {
                    return Status::bufferOverflow;
                }
                output[writeIndex++] = input[i];
            }
        }

        return writeIndex;
    }


    Status::info<size_t> Unescape(const uint8* input, size_t length, uint8* output, size_t maxOutputLength) const {
        size_t writeIndex = 0;

        for (size_t i = 0; i < length; ++i) {
            if (input[i] == config.escapeByte) {
                ++i; 
                if (i >= length) {
                    return Status::dataCorrupted;
                }
                if (input[i] == config.escapedStart) {
                    if (writeIndex + 1 > maxOutputLength) {
                        return Status::bufferOverflow;
                    }
                    output[writeIndex++] = config.startByte;
                } else if (input[i] == config.escapedStop) {
                    if (writeIndex + 1 > maxOutputLength) {
                        return Status::bufferOverflow;
                    }
                    output[writeIndex++] = config.stopByte;
                } else {
                    return Status::dataCorrupted;
                }
            } else {
                if (writeIndex + 1 > maxOutputLength) {
                    return Status::bufferOverflow;
                }
                output[writeIndex++] = input[i];
            }
        }

        return writeIndex;
    }
};
