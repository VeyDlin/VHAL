#pragma once
#include <System/System.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <span>
#include <array>
#include <cstring>
#include <algorithm>
#include <functional>


template<size_t MaxPacketSize = 20, size_t MaxConcurrentStreams = 4>
class CompactStreamingProtocol {
public:
    enum class PacketType : uint8 {
        Data = 0,
        Ack = 1
    };

    struct CompactPacketHeader {
        uint8 typeAndFlags;      // 1 bit type (0=data, 1=ack) + 7 bits flags/reserved
        uint8 payloadSize;       // 0-255 bytes
        uint8 fragmentIndex;     // 0-255 cycling fragment number
    } _APacked;

    static constexpr size_t HEADER_SIZE = sizeof(CompactPacketHeader);
    static constexpr size_t CRC_SIZE = 1;
    static constexpr size_t MAX_PAYLOAD_SIZE = MaxPacketSize - HEADER_SIZE - CRC_SIZE;

    enum class SendStatus : uint8 {
        Success = 0,
        Busy = 1,
        Error = 2
    };

    enum class StreamResult : uint8 {
        Success = 0,
        Timeout = 1,
        Error = 2
    };

    struct SendResult {
        SendStatus status;
        uint8 reserved; // No stream ID needed
    };

    using TransmitCallback = std::function<bool(std::span<const uint8> data)>;
    using DataReceivedCallback = std::function<bool(uint8 fragmentIndex, std::span<const uint8> data)>;
    using StreamCompleteCallback = std::function<void(StreamResult result)>;

private:
    TransmitCallback transmitCallback;
    DataReceivedCallback dataReceivedCallback;
    StreamCompleteCallback streamCompleteCallback;
    
    RingBuffer<uint8, MaxPacketSize * 4> receiveBuffer;
    
    // Fragment tracking for incoming data (streaming)
    uint8 lastReceivedFragment = 255; // Initialize to invalid value
    
    // Fragment transmission state
    std::array<uint8, 512> txBuffer;
    size_t txDataSize = 0;
    uint8 txTotalFragments = 0;
    uint8 txCurrentFragment = 0;
    bool txInProgress = false;
    uint32 txStartTime = 0;

    static uint8 CalculateCRC8(std::span<const uint8> data) {
        uint8 crc = 0xFF;
        for (uint8 byte : data) {
            crc ^= byte;
            for (int i = 0; i < 8; i++) {
                if (crc & 0x80) {
                    crc = (crc << 1) ^ 0x07;  // CRC8-CCITT polynomial
                } else {
                    crc = crc << 1;
                }
            }
        }
        return crc;
    }

public:
    CompactStreamingProtocol() = default;
    
    CompactStreamingProtocol(TransmitCallback transmit, DataReceivedCallback onDataReceived, StreamCompleteCallback onStreamComplete)
        : transmitCallback(transmit), dataReceivedCallback(onDataReceived), streamCompleteCallback(onStreamComplete) {}

    void Reset() {
        receiveBuffer.Clear();
        lastReceivedFragment = 255;
        txInProgress = false;
        txCurrentFragment = 0;
        txTotalFragments = 0;
        txDataSize = 0;
    }

    void Process() {
        // Process incoming data and timeouts
        ProcessIncoming();
    }

    // Send data (automatically fragments if needed)
    SendResult Send(std::span<const uint8> data) {
        if (data.empty()) return {SendStatus::Success, 0};
        
        if (txInProgress) {
            return {SendStatus::Busy, 0};
        }
        
        if (data.size() <= MAX_PAYLOAD_SIZE) {
            // Single packet
            if (SendSinglePacket(data)) {
                return {SendStatus::Success, 0};
            } else {
                return {SendStatus::Error, 0};
            }
        } else {
            // Multiple fragments - start fragmented transmission
            if (StartFragmentedTransmission(data)) {
                return {SendStatus::Success, 0};
            } else {
                return {SendStatus::Error, 0};
            }
        }
    }

    // Receive data from transport
    void DataReceived(std::span<const uint8> data) {
        for (uint8 byte : data) {
            receiveBuffer.Push(byte);
        }
        ProcessIncoming();
    }

private:
    bool SendSinglePacket(std::span<const uint8> data) {
        uint8 packet[MaxPacketSize];
        
        CompactPacketHeader* header = reinterpret_cast<CompactPacketHeader*>(packet);
        header->typeAndFlags = static_cast<uint8>(PacketType::Data); // type=0, flags=0
        header->payloadSize = data.size();
        header->fragmentIndex = 0; // Single packet fragment index
        
        if (data.size() > 0) {
            std::memcpy(packet + HEADER_SIZE, data.data(), data.size());
        }
        
        size_t dataSize = HEADER_SIZE + data.size();
        
        // Add CRC
        uint8 crc = CalculateCRC8(std::span<const uint8>(packet, dataSize));
        packet[dataSize] = crc;
        
        size_t totalSize = dataSize + CRC_SIZE;

        bool success = transmitCallback(std::span<const uint8>(packet, totalSize));
        
        if (success && streamCompleteCallback) {
            streamCompleteCallback(StreamResult::Success);
        }
        
        return success;
    }


    void ProcessIncoming() {
        while (receiveBuffer.Size() >= HEADER_SIZE + CRC_SIZE) {
            // Peek header
            uint8 header_bytes[HEADER_SIZE];
            for (size_t i = 0; i < HEADER_SIZE; i++) {
                header_bytes[i] = receiveBuffer[i];
            }
            
            CompactPacketHeader* header = reinterpret_cast<CompactPacketHeader*>(header_bytes);
            size_t totalSize = HEADER_SIZE + header->payloadSize + CRC_SIZE;
            
            if (receiveBuffer.Size() < totalSize) {
                break; // Not enough data
            }
            
            // Extract full packet
            uint8 packet[MaxPacketSize];
            for (size_t i = 0; i < totalSize; i++) {
                auto byte = receiveBuffer.Pop();
                if (byte.IsOk()) {
                    packet[i] = byte.data;
                } else {
                    return;
                }
            }
            
            // Verify CRC
            size_t dataSize = totalSize - CRC_SIZE;
            uint8 receivedCrc = packet[dataSize];
            uint8 calculatedCrc = CalculateCRC8(std::span<const uint8>(packet, dataSize));
            
            if (receivedCrc != calculatedCrc) {
                continue;
            }
            
            ProcessPacket(reinterpret_cast<CompactPacketHeader*>(packet), 
                         std::span<const uint8>(packet + HEADER_SIZE, header->payloadSize));
        }
    }

    void ProcessPacket(CompactPacketHeader* header, std::span<const uint8> payload) {
        PacketType type = static_cast<PacketType>(header->typeAndFlags & 1);
        
        if (type == PacketType::Ack) {
            // Handle ACK - continue transmission
            HandleAck(header->fragmentIndex);
            return;
        }
        
        // For data packets - always send ACK first
        SendAck(header->fragmentIndex);
        
        // Pass data to callback immediately (streaming approach)
        if (dataReceivedCallback) {
            dataReceivedCallback(header->fragmentIndex, payload);
        }
        
        // Stream completion detection can be done by:
        // 1. Empty payload (end marker)
        // 2. Timeout in upper layer
        // 3. Application-specific logic
        if (payload.empty() && streamCompleteCallback) {
            streamCompleteCallback(StreamResult::Success);
        }
    }

    
    void SendAck(uint8 fragmentIndex) {
        uint8 packet[MaxPacketSize];
        CompactPacketHeader* header = reinterpret_cast<CompactPacketHeader*>(packet);
        
        header->typeAndFlags = static_cast<uint8>(PacketType::Ack); // type=1
        header->payloadSize = 0;
        header->fragmentIndex = fragmentIndex;
        
        size_t dataSize = HEADER_SIZE;
        
        // Add CRC
        uint8 crc = CalculateCRC8(std::span<const uint8>(packet, dataSize));
        packet[dataSize] = crc;
        
        size_t totalSize = dataSize + CRC_SIZE;
        
        transmitCallback(std::span<const uint8>(packet, totalSize));
    }
    
    bool StartFragmentedTransmission(std::span<const uint8> data) {
        if (data.size() > txBuffer.size()) {
            return false;
        }
        
        txTotalFragments = (data.size() + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;
        
        // Copy data to buffer
        std::memcpy(txBuffer.data(), data.data(), data.size());
        txDataSize = data.size();
        txCurrentFragment = 0;
        txInProgress = true;
        
        // Send first fragment
        return SendNextFragment();
    }
    
    bool SendNextFragment() {
        if (!txInProgress || txCurrentFragment >= txTotalFragments) {
            return false;
        }
        
        size_t offset = txCurrentFragment * MAX_PAYLOAD_SIZE;
        size_t fragSize = std::min(static_cast<size_t>(MAX_PAYLOAD_SIZE), txDataSize - offset);
        
        uint8 packet[MaxPacketSize];
        CompactPacketHeader* header = reinterpret_cast<CompactPacketHeader*>(packet);
        header->typeAndFlags = static_cast<uint8>(PacketType::Data);
        header->payloadSize = fragSize;
        header->fragmentIndex = txCurrentFragment;
        
        std::memcpy(packet + HEADER_SIZE, txBuffer.data() + offset, fragSize);
        
        size_t dataSize = HEADER_SIZE + fragSize;
        
        // Add CRC
        uint8 crc = CalculateCRC8(std::span<const uint8>(packet, dataSize));
        packet[dataSize] = crc;
        
        size_t totalSize = dataSize + CRC_SIZE;

        if (!transmitCallback(std::span<const uint8>(packet, totalSize))) {
            txInProgress = false;
            return false;
        }
        
        return true;
    }
    
    void HandleAck(uint8 fragmentIndex) {
        if (!txInProgress) {
            return;
        }
        
        // Check if this ACK is for the fragment we just sent
        if (fragmentIndex == txCurrentFragment) {
            txCurrentFragment++;
            
            if (txCurrentFragment >= txTotalFragments) {
                // All fragments sent successfully
                txInProgress = false;
                
                if (streamCompleteCallback) {
                    streamCompleteCallback(StreamResult::Success);
                }
            } else {
                // Send next fragment
                if (!SendNextFragment()) {
                    txInProgress = false;
                    
                    if (streamCompleteCallback) {
                        streamCompleteCallback(StreamResult::Error);
                    }
                }
            }
        } else {
            // ACK for wrong fragment
        }
    }
};
