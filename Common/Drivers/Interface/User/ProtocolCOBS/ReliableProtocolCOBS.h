#pragma once
#define VHAL_RTOS
#define VHAL_RTOS_EVENT
#define VHAL_RTOS_MUTEX
#include <VHAL.h>
#include <Utilities/Data/ByteConverter.h>
#include <Utilities/Serialization/COBS/COBS.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <functional>
#include <cstring>
#include <utility>
#include <ranges>
#include <array>


template <size_t MaxPacketSize = 256, size_t AckMailCount = 10>
class ReliableProtocolCOBS {
public:
    std::function<ResultStatus(uint16 address, const uint8* data, size_t length)> onWrite = nullptr;
    std::function<ResultStatus(uint16 address, uint8* outData, size_t& outDataLength)> onRead = nullptr;
    std::function<ResultStatus(const uint8* encodedBuffer, size_t length)> rawWrite = nullptr;


private:
    enum PacketType : uint8 {
        ack = 0x01,
        nack = 0x02,
        write = 0x03,
        read = 0x04
    };

    static constexpr size_t maxEncodedSize = MaxPacketSize + MaxPacketSize / 254 + 3;

    struct AckInfo {
        uint32 id;
        PacketType packetType;
        bool overflow;
    } _APacked;

    COBS<maxEncodedSize> cobs;

    OS::MailBox<uint8, maxEncodedSize> byteMail;
    OS::MailBox<AckInfo, AckMailCount> ackMail;
    OS::Mutex writeMutex;

    uint8 rxBuffer[maxEncodedSize];
    size_t rxBufferCounter = 0;
    uint32 currentPacketId = 0;

    uint8* readBuffer;
    size_t readBufferLength;


public:
    //  COBS:   
    //      start byte      = 0xFF   
    //      stop byte       = 0x00  
    //      escape byte     = 0xFE
    //      escaped start   = 0x01
    //      escaped stop    = 0x02 
    ReliableProtocolCOBS() : cobs({0xFF, 0x00, 0xFE, 0x01, 0x02}), writeMutex() {}


    inline void RxEvent(uint8 byte) {
        byteMail.Put(byte);
    }


    void Execute(const std::chrono::milliseconds mailTimeOut = std::chrono::milliseconds(10)) {
        uint8 byte;
        if (!byteMail.Get(byte, mailTimeOut)) {
            return;
        }
        if (byte == cobs.GetConfig().startByte || rxBufferCounter >= maxEncodedSize) {
            rxBufferCounter = 0;
        }

        rxBuffer[rxBufferCounter++] = byte;

        if (byte == cobs.GetConfig().stopByte) {
            ProcessReceivedPacket();
            rxBufferCounter = 0;
        }
    }


    template <typename WriteDataType>
    ResultStatus Write(uint16 address, WriteDataType& data, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000), uint8 retry = 3) {
        return SendPacket(
            PacketType::write,
            address,
            reinterpret_cast<uint8*>(&data),
            sizeof(WriteDataType),
            nullptr,
            0,
            timeout,
            retry
        );
    }


    template <typename ReadDataType>
    Result<ReadDataType> Read(uint16 address, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000), uint8 retry = 3) {
        ReadDataType data;
        return Result<ReadDataType>::Capture(SendPacket(
            PacketType::read,
            address,
            nullptr,
            0,
            reinterpret_cast<uint8*>(&data),
            sizeof(ReadDataType),
            timeout,
            retry
        ), data);
    }


private:
    ResultStatus SendPacket(PacketType type, uint16 address, const uint8* data, size_t length, uint8* readData, size_t readLength, std::chrono::milliseconds timeout, uint8 retry) {
        if (length > MaxPacketSize) {
            return ResultStatus::dataCorrupted;
        }

        currentPacketId++;
        uint8 encodedBuffer[maxEncodedSize];
        auto packet = EncodePacket(type, address, currentPacketId, data, length, encodedBuffer, maxEncodedSize);
        if (packet.IsErr()) {
            return packet.Error();
        }

        readBuffer = readData;
        readBufferLength = readLength;

        ResultStatus outStatus = ResultStatus::retryExhausted;

        for (uint8 i = 0; i < retry; i++) {
            auto status = SendRawPacket(encodedBuffer, packet.Value());
            if (status != ResultStatus::ok) {
                outStatus = status;
                continue;
            }

            // Wait ack
            AckInfo ack;
            if (!ackMail.Get(ack, timeout)) {
                outStatus = ResultStatus::timeout;
                continue;
            }

            if (ack.id != currentPacketId) {
                outStatus = ResultStatus::dataCorrupted;
                continue;
            }

            outStatus = ack.packetType == PacketType::ack ? ResultStatus::ok : ResultStatus::nack;
            outStatus = ack.overflow ? ResultStatus::bufferOverflow : outStatus;

            break;
        }

        return outStatus;
    }


    ResultStatus SendRawPacket(const uint8* encodedBuffer, size_t length) {
    	if (rawWrite) {
            writeMutex.Lock();
            auto status = rawWrite(encodedBuffer, length);
            writeMutex.UnLock();
            return status;
    	}

    	return ResultStatus::writeError;
    }


    Result<size_t> EncodePacket(PacketType type, uint16 address, uint32 packetId, const uint8* data, size_t length, uint8* encodedBuffer, size_t encodedBufferSize) {
        // type
        encodedBuffer[0] = static_cast<uint8>(type); 

        // address
        memcpy(encodedBuffer + 1, &address, sizeof(address));

        // packetId
        memcpy(encodedBuffer + 1 + sizeof(address), &packetId, sizeof(packetId));

        // dataSize
        uint16 dataSize = static_cast<uint16>(length);
        memcpy(encodedBuffer + 1 + sizeof(address) + sizeof(packetId), &dataSize, sizeof(dataSize));

        // data
        if (data != nullptr && dataSize != 0) {
            memcpy(encodedBuffer + 1 + sizeof(address) + sizeof(packetId) + sizeof(dataSize), data, length);
        }

        // CRC
        uint16 crc = Crc::Calculate(encodedBuffer, 1 + sizeof(address) + sizeof(packetId) + sizeof(dataSize) + length, Crc::CRC_16_XMODEM());
        memcpy(encodedBuffer + 1 + sizeof(address) + sizeof(packetId) + sizeof(dataSize) + length, &crc, sizeof(crc));

        return cobs.Encode(encodedBuffer, 1 + sizeof(address) + sizeof(packetId) + sizeof(dataSize) + length + sizeof(crc), encodedBuffer, encodedBufferSize);
    }


    void ProcessReceivedPacket() {
        uint8 decodedBuffer[MaxPacketSize];
        auto result = cobs.Decode(rxBuffer, rxBufferCounter, decodedBuffer, MaxPacketSize);
        if (!result.IsOk() || result.Value() < 9) {
            return;
        }

        auto type = static_cast<PacketType>(decodedBuffer[0]);
        auto address = ByteConverter::GetType<uint16>(&decodedBuffer[1]);
        auto packetId = ByteConverter::GetType<uint32>(&decodedBuffer[3]);
        auto size = ByteConverter::GetType<uint16>(&decodedBuffer[7]);
        const uint8* data = decodedBuffer + 9;

        uint16 crc = ByteConverter::GetType<uint16>(&decodedBuffer[9 + size]);
        if (Crc::Calculate(decodedBuffer, 9 + size, Crc::CRC_16_XMODEM()) != crc) {
            return; // CRC error
        }


        // Get ACK/NACK
        if (type == PacketType::ack || type == PacketType::nack) {
            bool overflow = false;

            if (readBufferLength != 0 && readBuffer != nullptr) {
                if (readBufferLength <= size) {
                    memcpy(readBuffer, data, size);
                } else {
                    overflow = true;
                }
            }

            AckInfo ack { packetId, type, overflow };
            ackMail.Put(ack);
            return;
        }
        

        // Get data packet
        if (type == PacketType::read || type == PacketType::write) {
            uint8 encodedBuffer[maxEncodedSize];
            size_t encodedBufferSize = 0;

            ResultStatus mailStatus = ResultStatus::error;
            if (type == PacketType::read && onRead) {
                mailStatus = onRead(address, &encodedBuffer[0], encodedBufferSize);
            }

            if (type == PacketType::write && onWrite) {
                mailStatus = onWrite(address, data, size);
            }


            // Send ACK/NACK
            uint8 responseBuffer[maxEncodedSize];
            auto responseType = mailStatus == ResultStatus::ok ? PacketType::ack : PacketType::nack;

            auto response = EncodePacket(responseType, address, packetId, &encodedBuffer[0], encodedBufferSize, responseBuffer, maxEncodedSize);

            if (response.IsOk()) {
                SendRawPacket(responseBuffer, response.Value());
            }
        }
    }
};
