#pragma once
#include <System/System.h>
#include <Utilities/Data/FIFO/DynamicFifo.h> 
#include <Utilities/Data/ByteConverter.h> 
#include <Utilities/Data/COBS.h> 
#include <Utilities/Data/CRC.h> 
#include <functional>
#include <cstring> 
#include <utility>
#include <ranges>
#include <functional>


template <size_t MaxPacketSize = 256, size_t HeapSize = 512>
class ProtocolCOBS {
public:
    std::function<void()> onMail = nullptr;
    std::function<Status::statusType(uint8* buffer, uint32 size)> sendMessage = nullptr;
    static constexpr size_t maxEncodedSize = MaxPacketSize + MaxPacketSize / 254 + 3;

protected:
    DynamicFifo<HeapSize, uint16> mail;

private:
    COBS cobs;
    uint8 rxBuffer[maxEncodedSize];
    size_t rxBufferCounter = 0;


public:
    //  COBS:   
    //      start byte      = 0xFF   
    //      stop byte       = 0x00  
    //      escape byte     = 0xFE
    //      escaped start   = 0x01
    //      escaped stop    = 0x02 
    ProtocolCOBS(): cobs({0xFF, 0x00, 0xFE, 0x01, 0x02}) {}   


    virtual inline void RxEvent(uint8 byte) {
        if(byte == cobs.GetConfig().startByte){
            rxBufferCounter = 0;
            return;
        }

        if (rxBufferCounter >= maxEncodedSize) {
            rxBufferCounter = 0;
            return;
        }

        rxBuffer[rxBufferCounter++] = byte;

        if (byte == cobs.GetConfig().stopByte) {
            mail.Push<uint8>(rxBuffer, rxBufferCounter);
            rxBufferCounter = 0;
            if(onMail != nullptr) {
                onMail();
            }
        }
    }


    virtual Status::statusType SendPacket(uint16 address, const uint8* data, size_t length) {
        if (length > MaxPacketSize) {
            return Status::dataCorrupted; 
        }

        uint8 encodedBuffer[maxEncodedSize];

        auto packet = EncodePacket(address, data, length, &encodedBuffer, maxEncodedSize);
        if(packet.IsError()) {
            return packet.type;
        }

        return serial.WriteByteArray(encodedBuffer, packet.data);
    }


    virtual Status::type<std::pair<uint16, uint16>> Pop(uint8* data) {
        uint8 mailBuffer[maxEncodedSize];
        auto mailSize = mail.Pop<uint8>(mailBuffer);

        if(mailSize == 0) {
            return Status::empty;
        }

        uint8 decodedBuffer[MaxPacketSize];
        
        return DecodePacket(data, &mailBuffer, maxEncodedSize, &decodedBuffer, MaxPacketSize);
    }


protected:
    Status::info<size_t> EncodePacket(uint16 address, const uint8* data, size_t length, uint8* encodedBuffer, size_t encodedBufferSize) {
        // address
        memcpy(encodedBuffer, &address, sizeof(address));

        // dataSize
        uint16 dataSize = static_cast<uint16>(length);
        memcpy(encodedBuffer + sizeof(address), &dataSize, sizeof(dataSize));

        // data
        memcpy(encodedBuffer + sizeof(address) + sizeof(dataSize), data, length);

        // CRC
        uint16 crc = Crc::Calculate(encodedBuffer, sizeof(address) + sizeof(dataSize) + length, Crc::CRC_16_USB());
        memcpy(encodedBuffer + sizeof(address) + sizeof(dataSize) + length, &crc, sizeof(crc));

        auto encode = cobs.Encode(encodedBuffer, sizeof(address) + sizeof(dataSize) + length + sizeof(crc), encodedBuffer, encodedBufferSize);

        return encode;
    }


    Status::type<std::pair<uint16, uint16>> DecodePacket(uint8* data, const uint8* mailBuffer, size_t mailSize, uint8* decodedBuffer, size_t decodedBufferSize) {
        auto decode = cobs.Decode(mailBuffer, mailSize, decodedBuffer, decodedBufferSize);
        if (!decode.IsOk()) {
            return decode.type;
        }

        if (decode.data < 6) {
            return Status::dataCorrupted; 
        }
 
        uint16 address = ByteConverter::GetType<uint16>(decodedBuffer[0]);
        uint16 size = ByteConverter::GetType<uint16>(decodedBuffer[2]);

        if(decode.data != size + 6) {
            return Status::dataCorrupted;
        }

        auto crc = ByteConverter::GetType<uint16>(decodedBuffer[decode.data - 2]);
        if(Crc::Calculate(decodedBuffer, decode.data - 2, Crc::CRC_16_USB()) != crc) {
            return Status::crcError;

        }

        memcpy(&data, decodedBuffer[4], size);

        return { address, size };
    }


    Status::statusType SendMessage(uint8* buffer, uint32 size) {
        if(sendMessage != nullptr) {
            return sendMessage(uint8* buffer, uint32 size);
        }
    }
};