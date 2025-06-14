#pragma once
#include <System/System.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <span>
#include <array>
#include <cstring>
#include <algorithm>
#include <functional>


template<size_t MaxPacketSize = 20, size_t MaxPacketCount = 4>
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
    
    RingBuffer<uint8, MaxPacketSize * MaxPacketCount> receiveBuffer; // TODO: 4??
    
    // Fragment tracking for incoming data (streaming) with deduplication
    uint8 lastReceivedFragment = 255; // Initialize to invalid value  
    std::array<uint8, MAX_PAYLOAD_SIZE> lastFragmentData;
    size_t lastFragmentSize = 0;
    
    // Fragment transmission state
    std::array<uint8, 512> txBuffer;
    size_t txDataSize = 0;
    uint8 txTotalFragments = 0;
    uint8 txCurrentFragment = 0;
    bool txInProgress = false;
    uint32 txStartTime = 0;
    
    // Retry state
    uint32 txFragmentSentTime = 0;
    uint8 txRetries = 0;
    static constexpr uint8 MAX_RETRIES = 3;
    static constexpr uint32 FRAGMENT_TIMEOUT_MS = 5000; // 5 seconds

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
        lastFragmentSize = 0;
        txInProgress = false;
        txCurrentFragment = 0;
        txTotalFragments = 0;
        txDataSize = 0;
        txFragmentSentTime = 0;
        txRetries = 0;
    }

    void Process() {
        // Process incoming data and timeouts
        ProcessIncoming();
        
        // Debug: Show that Process() is being called during transmission
        if (txInProgress) {
            static uint32 lastDebugTime = 0;
            uint32 currentTime = System::GetMs();
            if (currentTime - lastDebugTime > 1000) { // Every 1 second
                lastDebugTime = currentTime;
            }
        }
        
        // Check for fragment timeout during transmission
        if (txInProgress && txFragmentSentTime > 0) {
            uint32 currentTime = System::GetMs();
            if (currentTime - txFragmentSentTime > FRAGMENT_TIMEOUT_MS) {
                // Fragment timeout - retry or fail
                if (txRetries < MAX_RETRIES) {
                    txRetries++;
                    // Retry current fragment
                    RetryCurrentFragment();
                } else {
                    // Max retries exceeded
                    txInProgress = false;
                    if (streamCompleteCallback) {
                        streamCompleteCallback(StreamResult::Timeout);
                    }
                }
            }
        }
    }

    // Send data (unified fragmentation logic for 1+ fragments)
    SendResult Send(std::span<const uint8> data) {
        if (data.empty()) return {SendStatus::Success, 0};
        
        if (txInProgress) {
            return {SendStatus::Busy, 0};
        }
        
        // ALL data goes through unified fragmentation logic
        // Single packet is just fragmented transmission with 1 fragment
        if (StartFragmentedTransmission(data)) {
            return {SendStatus::Success, 0};
        } else {
            return {SendStatus::Error, 0};
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
        
        // Check for duplicate fragments - prevent data corruption from retries
        bool isDuplicate = false;
        if (header->fragmentIndex == lastReceivedFragment && 
            payload.size() == lastFragmentSize &&
            payload.size() <= MAX_PAYLOAD_SIZE &&
            std::equal(payload.begin(), payload.end(), lastFragmentData.begin())) {
            // This is a duplicate fragment - ignore data but ACK was already sent
            isDuplicate = true;
        }
        
        if (!isDuplicate) {
            // New fragment - save it and pass to callback
            lastReceivedFragment = header->fragmentIndex;
            lastFragmentSize = payload.size();
            if (payload.size() <= MAX_PAYLOAD_SIZE) {
                std::copy(payload.begin(), payload.end(), lastFragmentData.begin());
            }
            
            // Pass data to callback (streaming approach)
            if (dataReceivedCallback) {
                dataReceivedCallback(header->fragmentIndex, payload);
            }
        }
        
        // Stream completion detection can be done by:
        // 1. Empty payload (end marker)
        // 2. Timeout in upper layer
        // 3. Application-specific logic
        if (!isDuplicate && payload.empty() && streamCompleteCallback) {
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
        txRetries = 0;
        txFragmentSentTime = 0;
        
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
            // Don't abort transmission - let timeout/retry mechanism handle this
            txFragmentSentTime = System::GetMs(); // Still set timeout for retry
            return false; // Return false to indicate failure, but keep txInProgress = true
        }
        
        // Record send time for timeout detection
        txFragmentSentTime = System::GetMs();
        
        return true;
    }
    
    bool RetryCurrentFragment() {
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
            // Don't abort transmission - let timeout/retry mechanism handle this
            txFragmentSentTime = System::GetMs(); // Still set timeout for retry
            return false; // Return false to indicate failure, but keep txInProgress = true
        }
        
        // Record send time for timeout detection
        txFragmentSentTime = System::GetMs();

        return true;
    }
    
    void HandleAck(uint8 fragmentIndex) {
        if (!txInProgress) {
            return;
        }
        
        // Check if this ACK is for the fragment we just sent
        if (fragmentIndex == txCurrentFragment) {
            // Reset retry counter for next fragment
            txRetries = 0;
            txFragmentSentTime = 0;
            
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
