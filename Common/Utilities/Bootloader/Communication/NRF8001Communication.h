#pragma once
#include "../ICommunication.h"
#include <Utilities/Buffer/RingBuffer.h>
#include <Drivers/Wireless/NRF8001/BLEPeripheral.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Data/ByteConverter.h>
#include <array>
#include <cstring>

#include <Utilities/Console/Console.h>
// ICommunication implementation for NRF8001 BLE chip
// PURPOSE: concrete transport implementation via Bluetooth Low Energy
// FEATURES:
// - Uses polling for SPI queries (not interrupts)
// - Implements reliable packet wrapper protocol with CRC16 and sequence numbers
// - Automatically splits data into BLE packets (15 bytes payload + 5 bytes overhead)
// - Accumulates incoming packets in ring buffer
// - Requires Process() call every few milliseconds
//
// WRAPPER PROTOCOL:
// Each BLE packet contains: [0xCC][SEQ][TYPE][LEN][PAYLOAD...][CRC16_LO][CRC16_HI]
// - 0xCC: start marker
// - SEQ: sequence number (0-255, wrapping)
// - TYPE: packet type (Data=0x00, Ack=0x01, Nack=0x02, ResendFrom=0x03)
// - LEN: payload length (0-14 bytes)
// - PAYLOAD: actual data
// - CRC16: checksum of payload only (little endian)
//
// RELIABILITY FEATURES:
// - Automatic ACK for every data packet
// - Timeout-based retransmission (300ms, 3 retries)
// - NACK for corrupted packets
// - ResendFrom for out-of-order detection
// - Sliding window (4 packets) for flow control
// - Duplicate detection
// CONNECTION:
// - SPI: standard 4-wire interface (MOSI, MISO, SCK, CS)
// - REQN: request from NRF8001 (active low)
// - RDYN: NRF8001 ready (active low)
// - RESET: chip reset (active low)
// IMPORTANT: Process() must be called frequently enough to not miss BLE events
// TEMPLATE PARAMETERS:
// - BufferSize: TX/RX buffer size (default 512)
template<size_t BufferSize = 512>
class NRF8001Communication : public ICommunication {
protected:
	ASPI& spi;
    AGPIO& reqnPin;    // Request pin (from NRF8001)
    AGPIO& rdynPin;    // Ready pin (from NRF8001)
    AGPIO& resetPin;   // Reset pin (to NRF8001)

    BLEPeripheral ble;
    std::optional<BLECentral> central;
    
    RingBuffer<uint8, BufferSize> txBuffer;
    RingBuffer<uint8, BufferSize> incomingBuffer;  // Buffer for incoming bytes to process one at a time
    
    bool connected = false;
    bool centralInit = false;
    DataReceivedCallback dataCallback;
    
    static constexpr size_t BLE_PACKET_SIZE = BLE_ATTRIBUTE_MAX_VALUE_LENGTH;  // Standard MTU for BLE (20 bytes)
    static constexpr size_t WRAPPER_OVERHEAD = 6;     // Header (4) + CRC16 (2) = 6 bytes
    static constexpr size_t PAYLOAD_SIZE = BLE_PACKET_SIZE - WRAPPER_OVERHEAD; // 14 bytes payload per wrapper packet
    
    // Packet types for reliable protocol
    enum class PacketType : uint8 {
        Data = 0x00,      // Normal data packet
        Ack = 0x01,       // Acknowledgment (contains sequence of received packet)
        Nack = 0x02,      // Negative acknowledgment (request retransmission)
        ResendFrom = 0x03 // Request to resend from specific sequence number
    };
    
    // Wrapper protocol for reliable transmission
    struct WrapperHeader {
        uint8 startMarker = 0xCC;    // Packet start marker
        uint8 sequence;              // Packet sequence number
        PacketType type;             // Packet type
        uint8 length;                // Payload length (0-14 bytes to fit type field)
    } _APacked;
    
    uint8 txSequence = 0;            // Outgoing sequence counter
    uint8 rxSequence = 0;            // Expected incoming sequence
    
    // Receive state machine
    enum class RxState {
        WaitingHeader,               // Waiting for start marker
        ReceivingPacket,             // Receiving packet data
        ProcessingPacket             // Processing complete packet
    } rxState = RxState::WaitingHeader;
    
    WrapperHeader currentHeader;
    std::array<uint8, BLE_PACKET_SIZE> rxPacketBuffer;
    size_t rxBytesReceived = 0;
    
    // Retransmission management
    struct PendingPacket {
        std::array<uint8, BLE_PACKET_SIZE> data;
        size_t size = 0;
        uint32 sentTime = 0;
        uint8 retryCount = 0;
        bool waitingAck = false;
    };
    
    static constexpr size_t TX_WINDOW_SIZE = 4;      // Sliding window size
    static constexpr uint32 ACK_TIMEOUT_MS = 300;    // 300ms timeout for ACK
    static constexpr uint8 MAX_RETRIES = 3;          // Maximum retry attempts
    
    std::array<PendingPacket, 256> txWindow;         // Indexed by sequence number
    uint8 txWindowStart = 0;                          // First unacknowledged sequence
    uint8 txWindowEnd = 0;                            // Next sequence to send
    
    // Last received sequences for duplicate detection
    std::array<bool, 256> rxReceived{};              // Tracks which sequences we've seen
    uint8 rxWindowStart = 0;                          // First expected sequence
    
    // Pending control packets to send (non-blocking)
    struct PendingControl {
        PacketType type = PacketType::Data;
        uint8 targetSeq = 0;
        bool pending = false;
    };
    std::array<PendingControl, 4> pendingControls;   // Small queue for ACK/NACK
    size_t pendingControlIndex = 0;
    
    // CRC16 table for fast calculation
    static inline const auto crc16Table = Crc::CRC_16_MODBUS().MakeTable();
    static constexpr uint32 POLL_INTERVAL_MS = 100;  // Poll interval
    
    static NRF8001Communication* instance;
    
    BLEService bootloaderService{"00004242-0000-1000-8000-00805F9B34FB"};
    BLEDescriptor bootloaderDescriptor{"2901", "Bootloader"};
    
    BLECharacteristic rxCharacteristic{"00004243-0000-1000-8000-00805F9B34FB", BLEWrite, BLE_PACKET_SIZE};
    BLEDescriptor rxDescriptor{"2901", "RX"};
    
    BLECharacteristic txCharacteristic{"00004244-0000-1000-8000-00805F9B34FB", BLENotify, BLE_PACKET_SIZE};
    BLEDescriptor txDescriptor{"2901", "TX"};
    
    const char* deviceName;

public:
    NRF8001Communication(
    	ASPI& spiAdapter,
		AGPIO& reqn,
		AGPIO& rdyn,
		AGPIO& reset,
		const char* customDeviceName = "Bootloader"
	) :	spi(spiAdapter),
		reqnPin(reqn),
		rdynPin(rdyn),
		resetPin(reset),
		ble(static_cast<ASPI*>(&spiAdapter), static_cast<AGPIO*>(&reqn), static_cast<AGPIO*>(&rdyn), static_cast<AGPIO*>(&reset)),
		deviceName(customDeviceName)
    { }



    virtual ~NRF8001Communication() = default;



    virtual Status::statusType Initialize() override {
    	SetupBootloaderService();
        
        ble.setLocalName(deviceName);
        ble.setDeviceName(deviceName);
        
        instance = this;
        
        ble.begin();

        return Status::ok;
    }



    virtual void Process() override {
        if (!central.has_value() || !*central || !central->connected()) {
            central = ble.central();
        }

        bool wasConnected = connected;
        connected = *central && central->connected();
        
        if (connected && !wasConnected) {
        	System::console << Console::separator() << Console::endl;
        	System::console << Console::debug << "Connected" << Console::endl;
        	OnConnected();
        } else if (!connected && wasConnected) {
        	System::console << Console::debug << "Disconnected" << Console::endl;
        	OnDisconnected();
        }

        if (connected) {
        	ProcessRetransmissions();   // Check for timeouts and retransmit
        	ProcessPendingControls();   // Send pending ACK/NACK
        	ProcessTxBuffer();
        	ProcessOneIncomingByte();  // Process one incoming byte per iteration
        }
    }



    virtual Status::statusType SendData(std::span<const uint8> data) override {
        if (!connected) {
            return Status::notReady;
        }
        
        System::console << Console::debug << "TX PUSH: " << data << Console::endl;
        
        // Split data into chunks and queue each chunk
        size_t offset = 0;
        while (offset < data.size()) {
            // Check if window is full
            uint8 windowSize = (txWindowEnd - txWindowStart) & 0xFF;
            if (windowSize >= TX_WINDOW_SIZE) {
                System::console << Console::debug << "TX window full, cannot send more" << Console::endl;
                return Status::noBufferSpaceAvailable;
            }
            
            size_t chunkSize = std::min(PAYLOAD_SIZE, data.size() - offset);
            
            // Create data packet
            if (!QueueDataPacket(data.subspan(offset, chunkSize))) {
                return Status::error;
            }
            
            offset += chunkSize;
        }
        
        return Status::ok;
    }



    virtual bool IsReadyToSend() const override {
        if (!connected) return false;
        
        // Check if window has space
        uint8 windowSize = (txWindowEnd - txWindowStart) & 0xFF;
        return windowSize < TX_WINDOW_SIZE && (txBuffer.GetFreeSpace() >= BLE_PACKET_SIZE);
    }



    virtual bool IsConnected() const override {
        return connected;
    }



    virtual void SetDataReceivedCallback(DataReceivedCallback callback) override {
        dataCallback = callback;
    }



    virtual size_t GetMaxPacketSize() const override {
        return PAYLOAD_SIZE;  // Return actual payload size (14 bytes)
    }

private:
    void OnConnected() {
        txBuffer.Clear();
        incomingBuffer.Clear();
        rxState = RxState::WaitingHeader;  // Reset RX state on connect
        rxSequence = 0;                    // Reset sequence counters
        txSequence = 0;
        
        // Reset transmission windows
        txWindowStart = 0;
        txWindowEnd = 0;
        rxWindowStart = 0;
        for (auto& packet : txWindow) {
            packet.size = 0;
            packet.waitingAck = false;
        }
        rxReceived.fill(false);
    }
    


    void OnDisconnected() {
        txBuffer.Clear();
        incomingBuffer.Clear();
        rxState = RxState::WaitingHeader;  // Reset RX state on disconnect
        rxSequence = 0;                    // Reset sequence counters
        txSequence = 0;
        
        // Reset transmission windows
        txWindowStart = 0;
        txWindowEnd = 0;
        rxWindowStart = 0;
        for (auto& packet : txWindow) {
            packet.size = 0;
            packet.waitingAck = false;
        }
        rxReceived.fill(false);
    }
    
    
    
    void ProcessIncomingData(std::span<const uint8> data) {
        // Store incoming data in a temporary buffer for processing one byte per Process() call
        for (uint8 byte : data) {
            if (incomingBuffer.GetFreeSpace() > 0) {
                incomingBuffer.Push(byte);
            }
        }
    }
    
    
    
    void ProcessOneIncomingByte() {
        if (incomingBuffer.IsEmpty()) {
            return;
        }
        
        auto result = incomingBuffer.Pop();
        if (!result.IsOk()) {
            return;
        }
        
        uint8 byte = result.data;
        
        switch (rxState) {
            case RxState::WaitingHeader:
                if (byte == 0xCC) {  // Start marker found
                    rxBytesReceived = 0;
                    rxPacketBuffer[rxBytesReceived++] = byte;
                    rxState = RxState::ReceivingPacket;
                }
                // Ignore any bytes that don't match start marker
                break;
                
            case RxState::ReceivingPacket:
                rxPacketBuffer[rxBytesReceived++] = byte;
                
                // Check if we have complete header
                if (rxBytesReceived >= sizeof(WrapperHeader)) {
                    // Extract header
                    std::memcpy(&currentHeader, rxPacketBuffer.data(), sizeof(WrapperHeader));
                    
                    // Check if we have complete packet (header + payload + CRC16)
                    size_t expectedSize = sizeof(WrapperHeader) + currentHeader.length + 2;
                    if (rxBytesReceived >= expectedSize) {
                        rxState = RxState::ProcessingPacket;
                        ProcessCompletePacket();
                        rxState = RxState::WaitingHeader;
                    }
                }
                
                // Prevent buffer overflow
                if (rxBytesReceived >= BLE_PACKET_SIZE) {
                    System::console << Console::debug << "RX buffer overflow, resetting" << Console::endl;
                    rxState = RxState::WaitingHeader;
                }
                break;
                
            case RxState::ProcessingPacket:
                // Should not happen
                rxState = RxState::WaitingHeader;
                break;
        }
    }
    
    
    
    void ProcessCompletePacket() {
        // Validate packet length
        if (currentHeader.length > PAYLOAD_SIZE) {
            System::console << Console::debug << "Invalid payload length: " << static_cast<int>(currentHeader.length) << Console::endl;
            SendNack(currentHeader.sequence);
            return;
        }
        
        // Extract payload and CRC
        size_t payloadOffset = sizeof(WrapperHeader);
        auto payload = std::span<const uint8>(rxPacketBuffer.data() + payloadOffset, currentHeader.length);
        
        size_t crcOffset = payloadOffset + currentHeader.length;
        uint16 receivedCrc = rxPacketBuffer[crcOffset] | (rxPacketBuffer[crcOffset + 1] << 8);
        
        // Verify CRC
        uint16 calculatedCrc = Crc::Calculate(payload.data(), payload.size(), crc16Table);
        if (receivedCrc != calculatedCrc) {
            System::console << Console::debug << "CRC mismatch: received=" << Console::hex(receivedCrc) 
                           << ", calculated=" << Console::hex(calculatedCrc) << Console::endl;
            SendNack(currentHeader.sequence);
            return;
        }
        
        // Process by packet type
        switch (currentHeader.type) {
            case PacketType::Data:
                ProcessDataPacket(currentHeader.sequence, payload);
                break;
                
            case PacketType::Ack:
                ProcessAckPacket(currentHeader.sequence, payload);
                break;
                
            case PacketType::Nack:
                ProcessNackPacket(currentHeader.sequence, payload);
                break;
                
            case PacketType::ResendFrom:
                ProcessResendFromPacket(currentHeader.sequence, payload);
                break;
                
            default:
                System::console << Console::debug << "Unknown packet type: " << static_cast<int>(currentHeader.type) << Console::endl;
                break;
        }
    }


    static void RxCharacteristicWritten(BLECentral& central, BLECharacteristic& characteristic) {
        if (instance) {
        	auto data = std::span<const uint8>(characteristic.value(), characteristic.valueLength());
        	System::console << Console::debug << "RX: " << data << Console::endl;
            instance->ProcessIncomingData(data);
        }
    }
    
    

    void SetDeviceName(const char* name) {
        deviceName = name;
    }
    
    

    const char* GetDeviceName() const {
        return deviceName;
    }



    void ProcessTxBuffer() {
        static uint32 lastSendTime = 0;
        static size_t currentPacketSize = 0;
        static std::array<uint8, BLE_PACKET_SIZE> currentPacket;
        
        if (!txCharacteristic.canNotify()) {
            return;
        }
        
        uint32 now = System::GetMs();
        
        // Accumulate bytes into current packet (non-blocking)
        while (!txBuffer.IsEmpty() && currentPacketSize < BLE_PACKET_SIZE) {
            auto result = txBuffer.Pop();
            if (result.IsOk()) {
                currentPacket[currentPacketSize++] = result.data;
            }
            
            // Limit bytes processed per call to avoid blocking
            if (currentPacketSize >= 4) {  // Process max 4 bytes per call
                break;
            }
        }
        
        // Send packet if complete or timeout
        bool shouldSend = false;
        if (currentPacketSize >= BLE_PACKET_SIZE) {
            shouldSend = true;  // Packet full
        } else if (currentPacketSize > 0 && (now - lastSendTime >= POLL_INTERVAL_MS)) {
            shouldSend = true;  // Timeout with partial data
        }
        
        if (shouldSend && currentPacketSize > 0) {
            auto tx = txCharacteristic.setValue(currentPacket.data(), currentPacketSize);
            System::console << Console::debug << "TX(" << tx << "): " << Console::hex(currentPacket.data(), currentPacketSize) << Console::endl;
            
            lastSendTime = now;
            currentPacketSize = 0;  // Reset for next packet
        }
    }



    void SetupBootloaderService() {
        ble.addAttribute(bootloaderService);
        ble.addAttribute(bootloaderDescriptor);
        ble.setAdvertisedServiceUuid(bootloaderService.uuid());
        
        ble.addAttribute(rxCharacteristic);
        ble.addAttribute(rxDescriptor);
        rxCharacteristic.setEventHandler(BLEWritten, RxCharacteristicWritten);
        
        ble.addAttribute(txCharacteristic);
        ble.addAttribute(txDescriptor);
    }
    


    static_assert(BufferSize >= BLE_PACKET_SIZE * 2, "BufferSize must be at least 2x BLE packet size (40 bytes)");
    
    
    
    // New methods for reliable protocol
    bool QueueDataPacket(std::span<const uint8> payload) {
        // Create wrapped packet
        WrapperHeader header;
        header.sequence = txSequence;
        header.type = PacketType::Data;
        header.length = static_cast<uint8>(payload.size());
        
        // Build complete packet
        auto& pending = txWindow[txSequence];
        pending.size = 0;
        
        // Add header
        auto headerBytes = reinterpret_cast<const uint8*>(&header);
        for (size_t i = 0; i < sizeof(header); i++) {
            pending.data[pending.size++] = headerBytes[i];
        }
        
        // Add payload
        for (uint8 byte : payload) {
            pending.data[pending.size++] = byte;
        }
        
        // Add CRC16
        uint16 crc = Crc::Calculate(payload.data(), payload.size(), crc16Table);
        pending.data[pending.size++] = static_cast<uint8>(crc & 0xFF);
        pending.data[pending.size++] = static_cast<uint8>((crc >> 8) & 0xFF);
        
        pending.sentTime = 0;  // Not sent yet
        pending.retryCount = 0;
        pending.waitingAck = true;
        
        // Advance sequence
        txSequence++;
        txWindowEnd = txSequence;
        
        return true;
    }
    
    
    
    void ProcessRetransmissions() {
        static uint8 currentCheckSeq = 0;  // Remember where we left off
        uint32 now = System::GetMs();
        
        // Process only ONE packet per call to avoid blocking
        uint8 windowSize = (txWindowEnd - txWindowStart) & 0xFF;
        if (windowSize == 0) {
            currentCheckSeq = txWindowStart;
            return;
        }
        
        // Start from where we left off
        uint8 seq = currentCheckSeq;
        
        // Find next packet that needs attention
        for (uint8 i = 0; i < windowSize; i++) {
            auto& packet = txWindow[seq];
            
            if (packet.waitingAck && packet.size > 0) {
                // Check if needs transmission or retransmission
                if (packet.sentTime == 0 || (now - packet.sentTime > ACK_TIMEOUT_MS)) {
                    if (packet.retryCount >= MAX_RETRIES) {
                        System::console << Console::error << "Packet " << static_cast<int>(seq) 
                                       << " failed after " << static_cast<int>(MAX_RETRIES) << " retries" << Console::endl;
                        // Mark as failed and advance window
                        packet.waitingAck = false;
                        if (seq == txWindowStart) {
                            AdvanceTxWindow();
                        }
                    } else if (txBuffer.GetFreeSpace() >= packet.size) {
                        // Send or resend packet
                        for (size_t i = 0; i < packet.size; i++) {
                            txBuffer.Push(packet.data[i]);
                        }
                        packet.sentTime = now;
                        packet.retryCount++;
                        
                        System::console << Console::debug << "Sent packet seq=" << static_cast<int>(seq) 
                                       << " retry=" << static_cast<int>(packet.retryCount) << Console::endl;
                    }
                    
                    // Process only one packet per call
                    currentCheckSeq = (seq + 1) & 0xFF;
                    return;
                }
            }
            
            seq = (seq + 1) & 0xFF;
            if (seq == txWindowEnd) {
                break;
            }
        }
        
        // Wrap around to start
        currentCheckSeq = txWindowStart;
    }
    
    
    
    void ProcessDataPacket(uint8 sequence, std::span<const uint8> payload) {
        System::console << Console::debug << "Received DATA seq=" << static_cast<int>(sequence) 
                       << " expected=" << static_cast<int>(rxWindowStart) << Console::endl;
        
        // Check for duplicate
        if (rxReceived[sequence]) {
            System::console << Console::debug << "Duplicate packet, sending ACK again" << Console::endl;
            SendAck(sequence);
            return;
        }
        
        // Check if in expected window
        int seqDiff = static_cast<int>(sequence) - static_cast<int>(rxWindowStart);
        if (seqDiff < 0) seqDiff += 256;  // Handle wrap-around
        
        if (seqDiff >= 128) {  // Too far behind
            System::console << Console::debug << "Old packet, ignoring" << Console::endl;
            return;
        }
        
        // Mark as received
        rxReceived[sequence] = true;
        
        // Send ACK immediately
        SendAck(sequence);
        
        // If this is the expected sequence, process and advance window
        if (sequence == rxWindowStart) {
            // Forward to upper layer
            if (dataCallback && payload.size() > 0) {
                dataCallback(payload);
            }
            
            // Advance window to next unreceived packet
            while (rxReceived[rxWindowStart]) {
                rxReceived[rxWindowStart] = false;  // Clear for wrap-around
                rxWindowStart++;
            }
        } else if (seqDiff > 0 && seqDiff < 16) {
            // Out of order but within reasonable range
            System::console << Console::debug << "Out of order packet, requesting resend from " 
                           << static_cast<int>(rxWindowStart) << Console::endl;
            SendResendFrom(rxWindowStart);
        }
    }
    
    
    
    void ProcessAckPacket(uint8 sequence, std::span<const uint8> payload) {
        if (payload.size() < 1) return;
        
        uint8 ackedSeq = payload[0];
        System::console << Console::debug << "Received ACK for seq=" << static_cast<int>(ackedSeq) << Console::endl;
        
        // Mark packet as acknowledged
        auto& packet = txWindow[ackedSeq];
        if (packet.waitingAck) {
            packet.waitingAck = false;
            packet.size = 0;  // Free the slot
            
            // Advance window if this was the oldest unacked packet
            if (ackedSeq == txWindowStart) {
                AdvanceTxWindow();
            }
        }
    }
    
    
    
    void ProcessNackPacket(uint8 sequence, std::span<const uint8> payload) {
        if (payload.size() < 1) return;
        
        uint8 nackedSeq = payload[0];
        System::console << Console::debug << "Received NACK for seq=" << static_cast<int>(nackedSeq) << Console::endl;
        
        // Force immediate retransmission
        auto& packet = txWindow[nackedSeq];
        if (packet.waitingAck && packet.size > 0) {
            packet.sentTime = 0;  // Force retransmission
        }
    }
    
    
    
    void ProcessResendFromPacket(uint8 sequence, std::span<const uint8> payload) {
        if (payload.size() < 1) return;
        
        uint8 resendFrom = payload[0];
        System::console << Console::debug << "Received RESEND_FROM seq=" << static_cast<int>(resendFrom) << Console::endl;
        
        // Mark all packets from resendFrom to current for retransmission
        for (uint8 seq = resendFrom; seq != txWindowEnd; seq++) {
            auto& packet = txWindow[seq];
            if (packet.waitingAck && packet.size > 0) {
                packet.sentTime = 0;  // Force retransmission
            }
        }
    }
    
    
    
    void SendAck(uint8 sequence) {
        QueueControlPacket(PacketType::Ack, sequence);
    }
    
    
    
    void SendNack(uint8 sequence) {
        QueueControlPacket(PacketType::Nack, sequence);
    }
    
    
    
    void SendResendFrom(uint8 sequence) {
        QueueControlPacket(PacketType::ResendFrom, sequence);
    }
    
    
    
    void QueueControlPacket(PacketType type, uint8 targetSequence) {
        // Find free slot
        for (auto& control : pendingControls) {
            if (!control.pending) {
                control.type = type;
                control.targetSeq = targetSequence;
                control.pending = true;
                return;
            }
        }
        // Queue full - overwrite oldest
        pendingControls[pendingControlIndex].type = type;
        pendingControls[pendingControlIndex].targetSeq = targetSequence;
        pendingControls[pendingControlIndex].pending = true;
        pendingControlIndex = (pendingControlIndex + 1) % pendingControls.size();
    }
    
    
    
    void ProcessPendingControls() {
        // Send one pending control packet per call
        for (auto& control : pendingControls) {
            if (control.pending) {
                if (txBuffer.GetFreeSpace() >= 8) {  // Need space for control packet
                    SendControlPacket(control.type, control.targetSeq);
                    control.pending = false;
                    return;  // Only one per call
                }
                break;  // No space, try later
            }
        }
    }
    
    
    
    void SendControlPacket(PacketType type, uint8 targetSequence) {
        if (txBuffer.GetFreeSpace() < 8) {  // Min size for control packet
            return;
        }
        
        WrapperHeader header;
        header.sequence = 0;  // Control packets don't use sequence
        header.type = type;
        header.length = 1;    // Contains target sequence
        
        // Build packet
        std::array<uint8, 8> packet;
        size_t size = 0;
        
        // Add header
        auto headerBytes = reinterpret_cast<const uint8*>(&header);
        for (size_t i = 0; i < sizeof(header); i++) {
            packet[size++] = headerBytes[i];
        }
        
        // Add target sequence
        packet[size++] = targetSequence;
        
        // Add CRC16
        uint16 crc = Crc::Calculate(&targetSequence, 1, crc16Table);
        packet[size++] = static_cast<uint8>(crc & 0xFF);
        packet[size++] = static_cast<uint8>((crc >> 8) & 0xFF);
        
        // Send immediately
        for (size_t i = 0; i < size; i++) {
            txBuffer.Push(packet[i]);
        }
        
        System::console << Console::debug << "Sent " << (type == PacketType::Ack ? "ACK" : 
                                                       type == PacketType::Nack ? "NACK" : "RESEND_FROM")
                       << " for seq=" << static_cast<int>(targetSequence) << Console::endl;
    }
    
    
    
    void AdvanceTxWindow() {
        // Advance start to next unacked packet
        while (txWindowStart != txWindowEnd && !txWindow[txWindowStart].waitingAck) {
            txWindowStart++;
        }
    }
};


using NRF8001CommunicationDefault = NRF8001Communication<>;
using NRF8001CommunicationSmall = NRF8001Communication<256>;
using NRF8001CommunicationLarge = NRF8001Communication<1024>;

template<size_t BufferSize>
NRF8001Communication<BufferSize>* NRF8001Communication<BufferSize>::instance = nullptr;


