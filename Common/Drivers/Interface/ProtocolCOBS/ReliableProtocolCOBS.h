#pragma once
#include <System/System.h>
#include <Utilities/Data/ByteConverter.h>
#include <Utilities/Data/COBS.h>
#include <Utilities/Data/CRC.h>
#include <Adapter/OSAdapter/RTOS.h>
#include <Adapter/OSAdapter/Thread.h>
#include <Adapter/OSAdapter/Event.h>
#include <Adapter/OSAdapter/Mutex.h>
#include <functional>
#include <cstring>
#include <utility>
#include <ranges>
#include <array>


template <size_t MaxPacketSize = 256, size_t AckMailCount = 5>
class ReliableProtocolCOBS {
public:
    std::function<bool(uint16 address, const uint8* data, size_t length)> onMail = nullptr;
    std::function<Status::statusType(const uint8* encodedBuffer, size_t length)> onWrite = nullptr;

private:
    enum PacketType : uint8 {
        ack = 0x01,
        nack = 0x02,
        data = 0x03
    };

    static constexpr size_t maxEncodedSize = MaxPacketSize + MaxPacketSize / 254 + 3;

    struct AckInfo {
        uint32 id;
        bool success;
    } _APacked;

    COBS cobs;

    OSAdapter::MailBox<uint8, maxEncodedSize> byteMail;
    OSAdapter::MailBox<AckInfo, AckMailCount> ackMail;
    OSAdapter::Mutex writeMutex;

    uint8 rxBuffer[maxEncodedSize];
    size_t rxBufferCounter = 0;
    uint32 currentPacketId = 0;


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


    void Execute(const std::chrono::milliseconds mailTimeOut = 10ms) {
        uint8 byte;
        if (!byteMail.Get(byte, mailTimeOut)) {
            return;
        }
        if (byte == cobs.GetConfig().startByte || rxBufferCounter >= maxEncodedSize) {
            rxBufferCounter = 0;
            return;
        }

        rxBuffer[rxBufferCounter++] = byte;

        if (byte == cobs.GetConfig().stopByte) {
            ProcessReceivedPacket();
            rxBufferCounter = 0;
        }
    }


    Status::statusType SendPacket(uint16 address, const uint8* data, size_t length, std::chrono::milliseconds timeout = 1000ms, uint8 retry = 3) {
        if (length > MaxPacketSize) {
            return Status::dataCorrupted;
        }

        currentPacketId++;
        uint8 encodedBuffer[maxEncodedSize];
        auto packet = EncodePacket(PacketType::data, address, currentPacketId, data, length, encodedBuffer, maxEncodedSize);
        if (packet.IsError()) {
            return packet.type;
        }

        Status::statusType outStatus = Status::retryExhausted;

        for(uint8 i = 0; i < retry; i++){
            auto status = SendRawPacket(encodedBuffer, packet.data);
            if (status != Status::ok) {
                outStatus = status;
                continue;
            }

            // Wait ack
            AckInfo ack;
            if (!ackMail.Get(ack, timeout)) {
                outStatus = Status::timeout;
                continue;
            }
            
            if (ack.id != currentPacketId) {
                outStatus = Status::dataCorrupted;
                continue;
            }
            
            if (ack.success) {
                outStatus = Status::ok;
                break;
            } else {
                outStatus = Status::nack;
            }
        }

        return outStatus;
    }


private:
    Status::statusType SendRawPacket(const uint8* encodedBuffer, size_t length) {
    	if (onWrite) {
            writeMutex.Lock();
            auto status = onWrite(encodedBuffer, length);
            writeMutex.UnLock();
            return status;
    	}

    	return Status::writeError;
    }


    Status::info<size_t> EncodePacket(PacketType type, uint16 address, uint32 packetId, const uint8* data, size_t length, uint8* encodedBuffer, size_t encodedBufferSize) {
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
        if (data != nullptr) {
            memcpy(encodedBuffer + 1 + sizeof(address) + sizeof(packetId) + sizeof(dataSize), data, length);
        }

        // CRC
        uint16 crc = Crc::Calculate(encodedBuffer, 1 + sizeof(address) + sizeof(packetId) + sizeof(dataSize) + length, Crc::CRC_16_USB());
        memcpy(encodedBuffer + 1 + sizeof(address) + sizeof(packetId) + sizeof(dataSize) + length, &crc, sizeof(crc));

        return cobs.Encode(encodedBuffer, 1 + sizeof(address) + sizeof(packetId) + sizeof(dataSize) + length + sizeof(crc), encodedBuffer, encodedBufferSize);
    }


    void ProcessReceivedPacket() {
        uint8 decodedBuffer[MaxPacketSize];
        auto result = cobs.Decode(rxBuffer, rxBufferCounter, decodedBuffer, MaxPacketSize);
        if (!result.IsOk() || result.data < 1) {
            return;
        }

        auto type = static_cast<PacketType>(decodedBuffer[0]);
        auto address = ByteConverter::GetType<uint16>(&decodedBuffer[1]);
        auto packetId = ByteConverter::GetType<uint32>(&decodedBuffer[3]);
        auto size = ByteConverter::GetType<uint16>(&decodedBuffer[7]);

        uint16 crc = ByteConverter::GetType<uint16>(&decodedBuffer[9 + size]);
        if (Crc::Calculate(decodedBuffer, 9 + size, Crc::CRC_16_USB()) != crc) {
            return; // CRC error
        }

        // Get ACK/NACK
        if (type == PacketType::ack || type == PacketType::nack) {
            AckInfo ack { packetId, type == PacketType::ack };
            ackMail.Put(ack);
            return;
        }
        
        // Get data packet
        if (type == PacketType::data) {
            uint16 size = ByteConverter::GetType<uint16>(&decodedBuffer[7]);
            const uint8* data = decodedBuffer + 9;
            bool success = onMail ? onMail(address, data, size) : false;

            // Send ACK/NACK
            uint8 responseBuffer[maxEncodedSize];
            auto responseType = success ? PacketType::ack : PacketType::nack;
            auto response = EncodePacket(responseType, address, packetId, nullptr, 0, responseBuffer, maxEncodedSize);
            if (response.IsOk()) {
                SendRawPacket(responseBuffer, response.data);
            }
        }
    }
};


