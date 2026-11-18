#pragma once
#include <VHAL.h>
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
        uint8 typeAndFlags;      // bit 0: type (0=data, 1=ack), bit 1: last fragment, bits 2-7: message id (0-63)
        uint8 payloadSize;       // 0-255 bytes
        uint8 fragmentIndex;     // 0-255 fragment number within the message
    } _APacked;

    // typeAndFlags layout (format v2)
    static constexpr uint8 typeMask = 0x01;
    static constexpr uint8 lastFragmentFlag = 0x02;
    static constexpr uint8 messageIdShift = 2;
    static constexpr uint8 messageIdMask = 0xFC;

    static constexpr uint8 MakeTypeAndFlags(PacketType type, uint8 messageId, bool lastFragment) {
        return static_cast<uint8>(static_cast<uint8>(type) |
                                  ((messageId << messageIdShift) & messageIdMask) |
                                  (lastFragment ? lastFragmentFlag : 0));
    }

    static constexpr PacketType GetPacketType(uint8 typeAndFlags) {
        return static_cast<PacketType>(typeAndFlags & typeMask);
    }

    static constexpr uint8 GetMessageId(uint8 typeAndFlags) {
        return static_cast<uint8>((typeAndFlags & messageIdMask) >> messageIdShift);
    }

    static constexpr bool IsLastFragment(uint8 typeAndFlags) {
        return (typeAndFlags & lastFragmentFlag) != 0;
    }

    static constexpr size_t HEADER_SIZE = sizeof(CompactPacketHeader);
    static constexpr size_t CRC_SIZE = 1;
    static constexpr size_t MAX_PAYLOAD_SIZE = MaxPacketSize - HEADER_SIZE - CRC_SIZE;

    // Capacity of the transmit buffer; Send() refuses a message that does not fit
    static constexpr size_t TX_BUFFER_SIZE = 512;

    // Fragment index and fragment count are uint8 on the wire and in txTotalFragments
    static_assert(MaxPacketSize > HEADER_SIZE + CRC_SIZE, "MaxPacketSize must leave room for a payload");
    static_assert((TX_BUFFER_SIZE + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE <= 255, "Fragment count must fit in uint8");

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
    using ReceiveResetCallback = std::function<void()>;

private:
    TransmitCallback transmitCallback;
    DataReceivedCallback dataReceivedCallback;
    StreamCompleteCallback streamCompleteCallback;
    ReceiveResetCallback receiveResetCallback;
    
    RingBuffer<uint8, MaxPacketSize * MaxPacketCount> receiveBuffer; // TODO: 4??
    
    // Receiver state: last accepted fragment (retransmissions are recognised by message id and index)
    bool rxHasLast = false;
    uint8 rxLastMessageId = 0;
    uint8 rxLastIndex = 0;
    // Receiver state: message in progress (its last fragment has not been accepted yet)
    bool rxMessageOpen = false;
    uint8 rxOpenMessageId = 0;
    uint8 rxNextIndex = 0;
    
    // Fragment transmission state
    std::array<uint8, TX_BUFFER_SIZE> txBuffer;
    size_t txDataSize = 0;
    uint8 txMessageId = 0;
    uint8 txTotalFragments = 0;
    uint8 txCurrentFragment = 0;
    bool txInProgress = false;
    uint32 txStartTime = 0;
    
    // Retry state
    uint32 txFragmentSentTime = 0;
    bool txTimerArmed = false; // True while txFragmentSentTime marks a transmitted, unacknowledged fragment
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
    
    CompactStreamingProtocol(TransmitCallback transmit,
                             DataReceivedCallback onDataReceived,
                             StreamCompleteCallback onStreamComplete,
                             ReceiveResetCallback onReceiveReset = nullptr)
        : transmitCallback(transmit),
          dataReceivedCallback(onDataReceived),
          streamCompleteCallback(onStreamComplete),
          receiveResetCallback(onReceiveReset)
    {
    }

    void Reset() {
        receiveBuffer.Clear();
        rxHasLast = false;
        rxMessageOpen = false;
        txInProgress = false;
        txCurrentFragment = 0;
        txTotalFragments = 0;
        txDataSize = 0;
        txFragmentSentTime = 0;
        txTimerArmed = false;
        txRetries = 0;
    }

    void Process() {
        // Process incoming data and timeouts
        ProcessIncoming();
        
        // Check for fragment timeout during transmission
        if (txInProgress && txTimerArmed) {
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
                    txTimerArmed = false;
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

    // True while a message is being transmitted and has not been acknowledged completely
    bool IsTransmitting() const {
        return txInProgress;
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

            // A payload that does not fit the packet buffer cannot be a valid fragment:
            // drop one byte and resynchronise on the next header
            if (header->payloadSize > MAX_PAYLOAD_SIZE) {
                receiveBuffer.Pop();
                continue;
            }

            size_t totalSize = HEADER_SIZE + header->payloadSize + CRC_SIZE;
            
            if (receiveBuffer.Size() < totalSize) {
                break; // Not enough data
            }
            
            // Peek the candidate packet; it is consumed only when its CRC matches
            uint8 packet[MaxPacketSize];
            uint16 peeked = receiveBuffer.PeekMultiple(packet, static_cast<uint16>(totalSize));
            if (peeked != totalSize) {
                break;
            }
            
            // Verify CRC of the candidate packet
            size_t dataSize = totalSize - CRC_SIZE;
            uint8 receivedCrc = packet[dataSize];
            uint8 calculatedCrc = CalculateCRC8(std::span<const uint8>(packet, dataSize));
            
            if (receivedCrc != calculatedCrc) {
                // Corrupted packet: drop one byte and resynchronise on the next header
                receiveBuffer.Pop();
                continue;
            }

            // Valid packet: consume it
            for (size_t i = 0; i < totalSize; i++) {
                receiveBuffer.Pop();
            }
            
            ProcessPacket(reinterpret_cast<CompactPacketHeader*>(packet), 
                         std::span<const uint8>(packet + HEADER_SIZE, header->payloadSize));
        }
    }

    void ProcessPacket(CompactPacketHeader* header, std::span<const uint8> payload) {
        PacketType type = GetPacketType(header->typeAndFlags);
        
        if (type == PacketType::Ack) {
            // Handle ACK - continue transmission
            HandleAck(GetMessageId(header->typeAndFlags), header->fragmentIndex);
            return;
        }
        
        uint8 messageId = GetMessageId(header->typeAndFlags);
        uint8 fragmentIndex = header->fragmentIndex;
        bool lastFragment = IsLastFragment(header->typeAndFlags);
        
        // Retransmission of the last accepted fragment (its ACK was lost): ACK again, nothing else
        if (rxHasLast && messageId == rxLastMessageId && fragmentIndex == rxLastIndex) {
            SendAck(messageId, fragmentIndex);
            return;
        }

        if (fragmentIndex == 0) {
            // New message. A message that never got its last fragment lost its tail: the receiver
            // resets its partial input before the new message is accepted
            if (rxMessageOpen && receiveResetCallback) {
                receiveResetCallback();
            }

            SendAck(messageId, fragmentIndex);
            rxMessageOpen = !lastFragment;
            rxOpenMessageId = messageId;
            rxNextIndex = 1;
        } else {
            // Continuation: accepted only in sequence, otherwise dropped without ACK (the sender times out)
            if (!rxMessageOpen || messageId != rxOpenMessageId || fragmentIndex != rxNextIndex) {
                return;
            }

            SendAck(messageId, fragmentIndex);
            rxNextIndex++;
            if (lastFragment) {
                rxMessageOpen = false;
            }
        }
        
        rxHasLast = true;
        rxLastMessageId = messageId;
        rxLastIndex = fragmentIndex;
            
        // Empty fragments are acknowledged and sequenced like any other, but deliver nothing
        if (!payload.empty() && dataReceivedCallback) {
            dataReceivedCallback(fragmentIndex, payload);
        }
    }

    
    void SendAck(uint8 messageId, uint8 fragmentIndex) {
        uint8 packet[MaxPacketSize];
        CompactPacketHeader* header = reinterpret_cast<CompactPacketHeader*>(packet);
        
        header->typeAndFlags = MakeTypeAndFlags(PacketType::Ack, messageId, false);
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
        txMessageId = static_cast<uint8>((txMessageId + 1) & 0x3F); // New message id, 0..63
        
        // Copy data to buffer
        std::memcpy(txBuffer.data(), data.data(), data.size());
        txDataSize = data.size();
        txCurrentFragment = 0;
        txInProgress = true;
        txRetries = 0;
        txFragmentSentTime = 0;
        txTimerArmed = false;
        
        // Send first fragment. A refused transmit is not fatal: the fragment timer retries it
        SendNextFragment();
        return true;
    }
    
    bool SendNextFragment() {
        if (!txInProgress || txCurrentFragment >= txTotalFragments) {
            return false;
        }
        
        size_t offset = txCurrentFragment * MAX_PAYLOAD_SIZE;
        size_t fragSize = std::min(static_cast<size_t>(MAX_PAYLOAD_SIZE), txDataSize - offset);
        
        uint8 packet[MaxPacketSize];
        CompactPacketHeader* header = reinterpret_cast<CompactPacketHeader*>(packet);
        header->typeAndFlags = MakeTypeAndFlags(PacketType::Data, txMessageId, txCurrentFragment + 1 == txTotalFragments);
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
            txFragmentSentTime = System::GetMs();
            txTimerArmed = true; // Still set timeout for retry
            return false; // Return false to indicate failure, but keep txInProgress = true
        }
        
        // Record send time for timeout detection
        txFragmentSentTime = System::GetMs();
        txTimerArmed = true;
        
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
        header->typeAndFlags = MakeTypeAndFlags(PacketType::Data, txMessageId, txCurrentFragment + 1 == txTotalFragments);
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
            txFragmentSentTime = System::GetMs();
            txTimerArmed = true; // Still set timeout for retry
            return false; // Return false to indicate failure, but keep txInProgress = true
        }
        
        // Record send time for timeout detection
        txFragmentSentTime = System::GetMs();
        txTimerArmed = true;

        return true;
    }
    
    void HandleAck(uint8 messageId, uint8 fragmentIndex) {
        if (!txInProgress) {
            return;
        }
        
        // Only the ACK of the fragment we are waiting for (same message id and index) advances the transmission
        if (messageId == txMessageId && fragmentIndex == txCurrentFragment) {
            // Reset retry counter for next fragment
            txRetries = 0;
            txFragmentSentTime = 0;
            txTimerArmed = false;
            
            txCurrentFragment++;
            
            if (txCurrentFragment >= txTotalFragments) {
                // All fragments sent successfully
                txInProgress = false;
                txTimerArmed = false;
                
                if (streamCompleteCallback) {
                    streamCompleteCallback(StreamResult::Success);
                }
            } else {
                // Send next fragment. A refused transmit is not fatal: txInProgress stays set
                // and the fragment timer in Process() retries the fragment
                SendNextFragment();
            }
        } else {
            // Stale or foreign ACK (earlier message or fragment): ignored
        }
    }
};
