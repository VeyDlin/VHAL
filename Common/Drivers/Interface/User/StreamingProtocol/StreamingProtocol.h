#pragma once
#include <System/System.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <span>
#include <array>
#include <optional>
#include <cstring>
#include <algorithm>
#include <functional>


/**
 * Reliable streaming protocol for microcontrollers with fragmentation support
 *
 * Features:
 * - Push-based data reception (call DataReceived() when data arrives)
 * - Automatic fragmentation for large messages
 * - ACK/NACK reliability with configurable retries
 * - CRC16 error detection
 * - No dynamic memory allocation
 * - Counter-based timeout (no time dependencies)
 * - std::function callbacks for flexibility
 *
 * Example usage:
 * // 1. Create protocol instance
 * StreamingProtocol<64> protocol(
 *     [](std::span<const uint8_t> data) -> bool {
 *         return UART_Send(data.data(), data.size()) == HAL_OK;
 *     },
 *     [](uint32_t streamId, uint16_t fragmentIndex, uint16_t totalFragments,
 *        std::span<const uint8_t> data) -> bool {
 *         ProcessReceivedData(data);
 *         return true; // Accept data
 *     },
 *     [](uint32_t streamId, StreamingProtocol::StreamResult result) -> void {
 *         if (result == StreamingProtocol::StreamResult::Success) {
 *             LED_SetGreen();
 *         }
 *     }
 * );
 *
 * // 2. In main loop
 * void MainLoop() {
 *     protocol.Process(); // Handle outgoing packets
 * }
 *
 * // 3. On UART interrupt
 * void UART_RxCallback(uint8_t* data, size_t length) {
 *     protocol.DataReceived(std::span<const uint8_t>(data, length));
 * }
 *
 * // 4. Send data
 * std::string message = "Hello World!";
 * auto data = std::span<const uint8_t>(
 *     reinterpret_cast<const uint8_t*>(message.data()),
 *     message.size()
 * );
 * auto result = protocol.Send(data);
*/
template<size_t MaxPacketSize = 32, size_t MaxPacketCount = 4>
class StreamingProtocol {
public:
    enum class PacketType : uint8 {
        SingleData = 0x01,     // Single data packet
        FragmentData = 0x02,   // Data fragment
        Ack = 0x03,            // Acknowledgment
        Nack = 0x04,           // Negative acknowledgment
        SingleNoAck = 0x05     // Single packet without acknowledgment
    };

    enum class StreamResult : uint8 {
        Success = 0,           // Success
        RetryExceeded = 1,     // Retry count exceeded
        Rejected = 2,          // Rejected by receiver
        Error = 3              // Protocol error
    };

    struct PacketHeader {
        uint16 packetNumber;
        PacketType type;
        uint16 payloadSize;

        // Fields for fragmentation (used only when type == FRAGMENT_DATA)
        uint32 streamId;       // Data stream ID
        uint16 totalFragments; // Total number of fragments
        uint16 fragmentIndex;  // Current fragment index (0-based)
    } _APacked;

    enum class SendStatus {
        Success,
        BufferFull
    };

    struct SendOptions {
        uint8 maxRetries = 3;
        bool retryOnError = true;

        constexpr SendOptions() = default;
        constexpr SendOptions(uint8 retries, bool onError = true)
            : maxRetries(retries),
			  retryOnError(onError)
        {}
    };

    struct SendResult {
        SendStatus status;
        uint32 streamId;          // Stream ID (for completion tracking)
        uint16 totalFragments;    // Number of fragments (0 for single packets)
    };

    // Callback for receiving data stream
    // Returns true if data is accepted, false if rejected
    using DataStreamCallback = std::function<bool(uint32 streamId, uint16 fragmentIndex, uint16 totalFragments, std::span<const uint8> data)>;

    // Callback for stream completion (success/error)
    using StreamCompleteCallback = std::function<void(uint32 streamId, StreamResult result)>;

    using TransmitCallback = std::function<bool(std::span<const uint8> data)>;


protected:
    static constexpr size_t MAX_PAYLOAD_SIZE = MaxPacketSize - sizeof(PacketHeader) - sizeof(uint16);
    static constexpr size_t BUFFER_SIZE = MaxPacketSize * MaxPacketCount;


public:
    static_assert(
        MaxPacketSize >= sizeof(PacketHeader) + 1 + sizeof(uint16),
        "MaxPacketSize must be at least header + 1 byte payload + CRC16"
    );

    StreamingProtocol() = default;

    StreamingProtocol(
        TransmitCallback transmit,
        DataStreamCallback onData,
        StreamCompleteCallback onComplete
    ): transmitCallback(transmit), onDataCallback(onData), onCompleteCallback(onComplete) { }


    // Main send method - automatically chooses single or stream mode
    SendResult Send(std::span<const uint8> data, const SendOptions& options = SendOptions{}) {
        if (data.size() <= MAX_PAYLOAD_SIZE) {
            // Send as single packet
            return SendSingle(data, PacketType::SingleData, options);
        } else {
            // Send as fragment stream
            return SendStream(data, options);
        }
    }


    // Send without acknowledgment (single packets only)
    SendResult SendNoAck(std::span<const uint8> data) {
        if (data.size() > MAX_PAYLOAD_SIZE) {
            return {SendStatus::BufferFull, 0, 0}; // Cannot fragment without ACK
        }
        SendOptions defaultOptions;
        return SendSingle(data, PacketType::SingleNoAck, defaultOptions);
    }


    // Process outgoing packets (call in main loop)
    void Process() {
        processCounter++;
        ProcessOutgoing();
        HandleRetransmissions();
        CheckStreamActivity();
    }


    // Receive data from external source (call when data received via UART/BLE/etc)
    void DataReceived(std::span<const uint8> data) {
        for (uint8 byte : data) {
            receiveBuffer.Push(byte);
        }
        ProcessIncoming();
    }

private:
    struct PendingPacket {
        PacketHeader header;
        std::array<uint8, MAX_PAYLOAD_SIZE> payload;
        uint32 createdAt;
        uint8 retryCount;
        bool requiresAck;
        SendOptions options;

        PendingPacket() : payload{}, createdAt(0), retryCount(0), requiresAck(false), options{} {}
    };

    struct ActiveStream {
        uint32 streamId;
        uint16 totalFragments;
        uint16 nextExpectedFragment;
        uint32 lastActivity;
        bool completed;

        ActiveStream() : streamId(0), totalFragments(0), nextExpectedFragment(0),
                        lastActivity(0), completed(true) {}
    };

    TransmitCallback transmitCallback;
    DataStreamCallback onDataCallback;
    StreamCompleteCallback onCompleteCallback;

    RingBuffer<uint8, BUFFER_SIZE> receiveBuffer;
    RingBuffer<PendingPacket, 16> pendingPackets;

    uint16 currentPacketNumber = 0;
    uint32 currentStreamId = 0;
    uint16 lastReceivedPacketNumber = 0;
    bool hasCurrentPendingPacket = false;
    PendingPacket currentPendingPacket;
    uint32 processCounter = 0;

    // Active incoming streams
    static constexpr size_t MAX_ACTIVE_STREAMS = 4;
    ActiveStream activeStreams[MAX_ACTIVE_STREAMS];


    // Send single packet
    SendResult SendSingle(std::span<const uint8> data, PacketType type, const SendOptions& options) {
        uint16 packetNum = currentPacketNumber++;
        auto result = SendSinglePacket(
            packetNum,
            type,
            data,
            type != PacketType::SingleNoAck,
            options
        );
        result.streamId = 0; // Single packets don't have streamId
        result.totalFragments = 0;
        return result;
    }


    // Send fragment stream
    SendResult SendStream(std::span<const uint8> data, const SendOptions& options) {
        uint32 streamId = currentStreamId++;
        size_t totalFragments = (data.size() + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;

        if (totalFragments > 0xFFFF) {
            return {SendStatus::BufferFull, streamId, 0};
        }

        // Send all fragments
        for (size_t i = 0; i < totalFragments; i++) {
            size_t offset = i * MAX_PAYLOAD_SIZE;
            size_t fragmentSize = std::min(MAX_PAYLOAD_SIZE, data.size() - offset);

            uint16 packetNum = currentPacketNumber++;
            auto fragmentData = data.subspan(offset, fragmentSize);

            auto result = SendFragmentPacket(
                packetNum,
                streamId,
                static_cast<uint16>(totalFragments),
                static_cast<uint16>(i),
                fragmentData,
                options
            );

            if (result.status != SendStatus::Success) {
                return {SendStatus::BufferFull, streamId, static_cast<uint16>(i)};
            }
        }

        return {SendStatus::Success, streamId, static_cast<uint16>(totalFragments)};
    }


    // Send fragment packet
    SendResult SendFragmentPacket(
        uint16 packetNumber,
        uint32 streamId,
        uint16 totalFragments,
        uint16 fragmentIndex,
        std::span<const uint8> data,
        const SendOptions& options
    ) {
        PendingPacket packet;
        packet.header.packetNumber = packetNumber;
        packet.header.type = PacketType::FragmentData;
        packet.header.payloadSize = static_cast<uint16>(data.size());
        packet.header.streamId = streamId;
        packet.header.totalFragments = totalFragments;
        packet.header.fragmentIndex = fragmentIndex;
        packet.createdAt = processCounter;
        packet.retryCount = 0;
        packet.requiresAck = true;
        packet.options = options;

        if (data.size() > 0) {
            std::copy(data.begin(), data.end(), packet.payload.begin());
        }

        if (pendingPackets.Push(packet) != Status::ok) {
            return {SendStatus::BufferFull, streamId, fragmentIndex};
        }

        return {SendStatus::Success, streamId, fragmentIndex};
    }


    // Send regular packet
    SendResult SendSinglePacket(
        uint16 packetNumber,
        PacketType type,
        std::span<const uint8> data,
        bool requiresAck,
        const SendOptions& options
    ) {
        PendingPacket packet;
        packet.header.packetNumber = packetNumber;
        packet.header.type = type;
        packet.header.payloadSize = static_cast<uint16>(data.size());
        packet.header.streamId = 0; // Not used for single packets
        packet.header.totalFragments = 0;
        packet.header.fragmentIndex = 0;
        packet.createdAt = processCounter;
        packet.retryCount = 0;
        packet.requiresAck = requiresAck;
        packet.options = options;

        if (data.size() > 0) {
            std::copy(data.begin(), data.end(), packet.payload.begin());
        }

        auto pushResult = pendingPackets.Push(packet);
        if (pushResult != Status::ok) {
            return {SendStatus::BufferFull, 0, 0};
        }

        return {SendStatus::Success, 0, 0};
    }


    ActiveStream* FindStream(uint32 streamId) {
        for (auto& stream : activeStreams) {
            if (stream.streamId == streamId && !stream.completed) {
                return &stream;
            }
        }
        return nullptr;
    }


    ActiveStream* AllocateStream(uint32 streamId) {
        // First look for free slot
        for (auto& stream : activeStreams) {
            if (stream.completed) {
                stream = ActiveStream();
                stream.streamId = streamId;
                stream.completed = false;
                return &stream;
            }
        }
        // If no free slots, take the oldest one
        ActiveStream* oldest = &activeStreams[0];
        uint32 oldestTime = oldest->lastActivity;
        for (auto& stream : activeStreams) {
            if (stream.lastActivity < oldestTime) {
                oldest = &stream;
                oldestTime = stream.lastActivity;
            }
        }

        // Complete old stream with error
        if (!oldest->completed) {
            onCompleteCallback(
                oldest->streamId,
                StreamResult::Error
            );
        }

        *oldest = ActiveStream();
        oldest->streamId = streamId;
        oldest->completed = false;
        return oldest;
    }


    void ProcessIncoming() {
        // Parse packets from buffer
        while (receiveBuffer.Size() >= sizeof(PacketHeader)) {
            PacketHeader header;
            if (!PeekHeader(header)) {
                break;
            }

            size_t totalSize = sizeof(PacketHeader) + header.payloadSize + sizeof(uint16);
            if (receiveBuffer.Size() < totalSize) {
                break;
            }

            if (!ProcessPacket(totalSize)) {
                receiveBuffer.Pop();
            }
        }
    }


    bool PeekHeader(PacketHeader& header) {
        if (receiveBuffer.Size() < sizeof(PacketHeader)) {
            return false;
        }

        uint8 headerBytes[sizeof(PacketHeader)];
        for (size_t i = 0; i < sizeof(PacketHeader); i++) {
            headerBytes[i] = receiveBuffer[i];
        }

        std::memcpy(
            &header,
            headerBytes,
            sizeof(PacketHeader)
        );
        return true;
    }


    bool ProcessPacket(size_t totalSize) {
        uint8 packetData[MaxPacketSize];

        for (size_t i = 0; i < totalSize; i++) {
            auto byte = receiveBuffer.Pop();
            if (byte.IsError()) return false;
            packetData[i] = byte.data;
        }

        // Check CRC
        uint16 receivedCrc;
        std::memcpy(
            &receivedCrc,
            &packetData[totalSize - sizeof(uint16)],
            sizeof(uint16)
        );

        uint16 calculatedCrc = CalculateCRC16(
            std::span<const uint8>(packetData, totalSize - sizeof(uint16))
        );

        if (receivedCrc != calculatedCrc) {
            PacketHeader header;
            std::memcpy(
                &header,
                packetData,
                sizeof(PacketHeader)
            );
            if (header.type == PacketType::SingleData || header.type == PacketType::FragmentData) {
                SendNack(header.packetNumber);
            }
            return false;
        }

        // Extract packet
        PacketHeader header;
        std::memcpy(
            &header,
            packetData,
            sizeof(PacketHeader)
        );
        std::span<const uint8> payload(
            packetData + sizeof(PacketHeader),
            header.payloadSize
        );

        // Process by type
        switch (header.type) {
            case PacketType::SingleData:
                HandleSingleData(header, payload);
                break;

            case PacketType::FragmentData:
                HandleFragmentData(header, payload);
                break;

            case PacketType::Ack:
            case PacketType::Nack:
                HandleAckNack(header, payload);
                break;

            case PacketType::SingleNoAck:
                HandleSingleNoAck(header, payload);
                break;

            default:
                return false;
        }

        return true;
    }


    void HandleSingleData(const PacketHeader& header, std::span<const uint8> payload) {
        if (header.packetNumber == lastReceivedPacketNumber) {
            SendAck(header.packetNumber);
            return;
        }

        lastReceivedPacketNumber = header.packetNumber;

        // Call callback as single message (streamId = 0)
        bool accepted = onDataCallback(
            0,
            0,
            1,
            payload
        );

        if (accepted) {
            SendAck(header.packetNumber);
            onCompleteCallback(
                0,
                StreamResult::Success
            );
        } else {
            SendNack(header.packetNumber);
            onCompleteCallback(
                0,
                StreamResult::Rejected
            );
        }
    }


    void HandleFragmentData(const PacketHeader& header, std::span<const uint8> payload) {
        uint32 streamId = header.streamId;
        uint16 totalFragments = header.totalFragments;
        uint16 fragmentIndex = header.fragmentIndex;

        // Validity checks
        if (fragmentIndex >= totalFragments) {
            SendNack(header.packetNumber);
            return;
        }

        // Find or create stream
        ActiveStream* stream = FindStream(streamId);
        if (!stream) {
            stream = AllocateStream(streamId);
            stream->totalFragments = totalFragments;
            stream->nextExpectedFragment = 0;
        }

        // Check consistency
        if (stream->totalFragments != totalFragments) {
            SendNack(header.packetNumber);
            return;
        }

        stream->lastActivity = processCounter;

        // Call callback - data is processed immediately
        bool accepted = onDataCallback(
            streamId,
            fragmentIndex,
            totalFragments,
            payload
        );

        if (accepted) {
            SendAck(header.packetNumber);

            // Check if stream is complete
            if (fragmentIndex == totalFragments - 1) {
                stream->completed = true;
                onCompleteCallback(
                    streamId,
                    StreamResult::Success
                );
            }
        } else {
            SendNack(header.packetNumber);
            stream->completed = true;
            onCompleteCallback(
                streamId,
                StreamResult::Rejected
            );
        }
    }


    void HandleSingleNoAck(const PacketHeader& header, std::span<const uint8> payload) {
        // Call callback as single message without acknowledgment
        onDataCallback(
            0,
            0,
            1,
            payload
        );
        onCompleteCallback(
            0,
            StreamResult::Success
        );
    }


    void HandleAckNack(const PacketHeader& header, std::span<const uint8> payload) {
        // Process acknowledgments - remove from pending queue
        RingBuffer<PendingPacket, 16> tempQueue;
        bool found = false;

        while (!pendingPackets.IsEmpty()) {
            auto packet = pendingPackets.Pop();
            if (!packet.IsOk()) break;

            if (!found && packet.data.header.packetNumber == header.packetNumber) {
                found = true;
                if (header.type == PacketType::Nack && packet.data.options.retryOnError) {
                    if (packet.data.retryCount < packet.data.options.maxRetries) {
                        packet.data.retryCount++;
                        packet.data.createdAt = processCounter;
                        tempQueue.Push(packet.data);
                    }
                }
                // If ACK or retries exceeded - don't return to queue
            } else {
                tempQueue.Push(packet.data);
            }
        }

        // Return packets back
        while (!tempQueue.IsEmpty()) {
            auto packet = tempQueue.Pop();
            if (packet.IsOk()) {
                pendingPackets.Push(packet.data);
            }
        }
    }


    void SendAck(uint16 packetNumber) {
        uint16 packetNum = currentPacketNumber++;
        SendOptions defaultOptions;
        SendSinglePacket(
            packetNum,
            PacketType::Ack,
            {},
            false,
            defaultOptions
        );
    }


    void SendNack(uint16 packetNumber) {
        uint16 packetNum = currentPacketNumber++;
        SendOptions defaultOptions;
        SendSinglePacket(
            packetNum,
            PacketType::Nack,
            {},
            false,
            defaultOptions
        );
    }


    void ProcessOutgoing() {
        if (hasCurrentPendingPacket) {
            return;
        }

        auto packet = pendingPackets.Peek();
        if (packet.IsError()) {
            return;
        }

        currentPendingPacket = packet.data;
        hasCurrentPendingPacket = true;
        pendingPackets.Pop();
    }


    void HandleRetransmissions() {
        if (!hasCurrentPendingPacket) {
            return;
        }

        // Assemble packet
        std::array<uint8, MaxPacketSize> packetData;
        size_t offset = 0;

        std::memcpy(
            packetData.data(),
            &currentPendingPacket.header,
            sizeof(PacketHeader)
        );
        offset += sizeof(PacketHeader);

        if (currentPendingPacket.header.payloadSize > 0) {
            std::memcpy(
                packetData.data() + offset,
                currentPendingPacket.payload.data(),
                currentPendingPacket.header.payloadSize
            );
            offset += currentPendingPacket.header.payloadSize;
        }

        uint16 crc = CalculateCRC16(std::span<const uint8>(packetData.data(), offset));
        std::memcpy(
            packetData.data() + offset,
            &crc,
            sizeof(uint16)
        );
        offset += sizeof(uint16);

        if (transmitCallback(std::span<const uint8>(packetData.data(), offset))) {
            hasCurrentPendingPacket = false;
        }
    }


    void CheckStreamActivity() {
        // Check stream activity (e.g., every 10000 Process() calls)
        static constexpr uint32 STREAM_TIMEOUT_CYCLES = 10000;

        for (auto& stream : activeStreams) {
            if (!stream.completed && processCounter - stream.lastActivity > STREAM_TIMEOUT_CYCLES) {
                stream.completed = true;
                onCompleteCallback(
                    stream.streamId,
                    StreamResult::RetryExceeded
                );
            }
        }
    }


    uint16 CalculateCRC16(std::span<const uint8> data) {
        return Crc::Calculate(data.data(), data.size(), Crc::CRC_16_CCITTFALSE());
    }
};

