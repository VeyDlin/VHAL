#pragma once
#include "../ICommunication.h"
#include <Drivers/Wireless/NRF8001/BLEPeripheral.h>
#include <Drivers/Interface/User/StreamingProtocol/StreamingProtocol.h>
#include <Utilities/Console/Console.h>
#include <Utilities/Data/ByteConverter.h>
#include <array>

// Simplified NRF8001Communication using StreamingProtocol
// This replaces the complex custom protocol with a proven, tested implementation
template<size_t MaxPacketCount = 4, size_t BufferSize = 512>
class NRF8001Communication : public ICommunication {
protected:
    static constexpr size_t BLE_PACKET_SIZE = BLE_ATTRIBUTE_MAX_VALUE_LENGTH;  // 20 bytes

    ASPI& spi;
    AGPIO& reqnPin;    // Request pin (from NRF8001)
    AGPIO& rdynPin;    // Ready pin (from NRF8001)
    AGPIO& resetPin;   // Reset pin (to NRF8001)

    BLEPeripheral ble;
    std::optional<BLECentral> central;
    
    // StreamingProtocol handles all the complexity!
    StreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount> protocol;
    
    bool connected = false;
    DataReceivedCallback dataCallback;
    
    // Buffer for accumulating fragmented messages
    std::array<uint8, BufferSize> messageBuffer;
    size_t messageSize = 0;
    uint32 currentStreamId = 0xFFFFFFFF;  // Invalid stream ID
    
    // BLE service setup
    BLEService bootloaderService{"00004242-0000-1000-8000-00805F9B34FB"};
    BLEDescriptor bootloaderDescriptor{"2901", "Bootloader"};
    
    BLECharacteristic rxCharacteristic{"00004243-0000-1000-8000-00805F9B34FB", BLEWrite, BLE_PACKET_SIZE};
    BLEDescriptor rxDescriptor{"2901", "RX"};
    
    BLECharacteristic txCharacteristic{"00004244-0000-1000-8000-00805F9B34FB", BLENotify, BLE_PACKET_SIZE};
    BLEDescriptor txDescriptor{"2901", "TX"};
    
    const char* deviceName = nullptr;
    static NRF8001Communication* instance;

public:
    NRF8001Communication(
        ASPI& spiAdapter,
        AGPIO& reqn,
        AGPIO& rdyn,
        AGPIO& reset,
        const char* customDeviceName = "Bootloader"
    ) : spi(spiAdapter),
        reqnPin(reqn),
        rdynPin(rdyn),
        resetPin(reset),
        ble(static_cast<ASPI*>(&spiAdapter), static_cast<AGPIO*>(&reqn), static_cast<AGPIO*>(&rdyn), static_cast<AGPIO*>(&reset)),
        deviceName(customDeviceName),
        protocol(
            // Transmit callback - send data via BLE
            [this](std::span<const uint8> data) -> bool {
                return this->SendBLEPacket(data);
            },
            // Data received callback - pass to ICommunication callback
            [this](uint32 streamId, uint16 fragmentIndex, uint16 totalFragments, std::span<const uint8> data) -> bool {
                return this->HandleReceivedData(streamId, fragmentIndex, totalFragments, data);
            },
            // Stream complete callback - log completion
            [this](uint32 streamId, StreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>::StreamResult result) -> void {
                this->HandleStreamComplete(streamId, result);
            }
        )
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
        // Handle BLE connection
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
            // Process incoming BLE data
            ProcessIncomingBLE();
            
            // Let StreamingProtocol handle all the protocol logic
            protocol.Process();
        }
    }

    virtual Status::statusType SendData(std::span<const uint8> data) override {
        if (!connected) {
            return Status::notReady;
        }
        
        System::console << Console::debug << "TX SEND: " << data << Console::endl;
        
        // StreamingProtocol handles fragmentation, ACK/NACK, retries automatically!
        auto result = protocol.Send(data);
        
        if (result.status == StreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>::SendStatus::Success) {
            return Status::ok;
        } else {
            return Status::noBufferSpaceAvailable;
        }
    }

    virtual void SetDataReceivedCallback(DataReceivedCallback callback) override {
        dataCallback = callback;
    }

    virtual bool IsConnected() const override {
        return connected;
    }

    virtual bool IsReadyToSend() const override {
        // Ready if connected and StreamingProtocol has buffer space
        return connected;  // StreamingProtocol manages its own buffers
    }

    virtual size_t GetMaxPacketSize() const override {
        // Return the payload size StreamingProtocol can handle
        // BLE packet size minus StreamingProtocol overhead (header + CRC)
        return BLE_PACKET_SIZE - sizeof(typename StreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>::PacketHeader) - sizeof(uint16);
    }

private:
    void SetupBootloaderService() {
        ble.addAttribute(bootloaderService);
        ble.addAttribute(bootloaderDescriptor);
        ble.setAdvertisedServiceUuid(bootloaderService.uuid());

        ble.addAttribute(rxCharacteristic);
        ble.addAttribute(rxDescriptor);
        rxCharacteristic.setEventHandler(BLEWritten, OnRxCharacteristicWritten);

        ble.addAttribute(txCharacteristic);
        ble.addAttribute(txDescriptor);
    }

    // BLE packet transmission - called by StreamingProtocol
    bool SendBLEPacket(std::span<const uint8> data) {
        if (!connected || data.size() > BLE_PACKET_SIZE) {
            return false;
        }
        
        System::console << Console::debug << "BLE TX: " << data << Console::endl;
        
        // Send via BLE notification
        if (txCharacteristic.setValue(data.data(), data.size())) {
            return true;
        }
        
        return false;
    }

    // Handle received data from StreamingProtocol
    bool HandleReceivedData(uint32 streamId, uint16 fragmentIndex, uint16 totalFragments, std::span<const uint8> data) {
        System::console << Console::debug << "RX DATA: stream=" << streamId 
                         << " frag=" << fragmentIndex << "/" << totalFragments 
                         << " data=" << data << Console::endl;
        
        // Single packet (no fragmentation)
        if (totalFragments == 1) {
            if (dataCallback) {
                dataCallback(data);
            }
            return true;
        }
        
        // Handle fragmentation
        if (streamId != currentStreamId || fragmentIndex == 0) {
            // New stream or restart
            currentStreamId = streamId;
            messageSize = 0;
        }
        
        // Check buffer space
        if (messageSize + data.size() > messageBuffer.size()) {
            System::console << Console::error << "Message buffer overflow!" << Console::endl;
            return false;
        }
        
        // Accumulate fragment
        std::copy(data.begin(), data.end(), messageBuffer.begin() + messageSize);
        messageSize += data.size();
        
        // If this is the last fragment, call the callback
        if (fragmentIndex == totalFragments - 1) {
            if (dataCallback) {
                dataCallback(std::span<const uint8>(messageBuffer.data(), messageSize));
            }
            messageSize = 0;
            currentStreamId = 0xFFFFFFFF;
        }
        
        return true; // Always accept data
    }

    // Handle stream completion
    void HandleStreamComplete(uint32 streamId, StreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>::StreamResult result) {
        if (result == StreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>::StreamResult::Success) {
            System::console << Console::debug << "Stream " << streamId << " completed successfully" << Console::endl;
        } else {
            System::console << Console::debug << "Stream " << streamId << " failed: " << static_cast<int>(result) << Console::endl;
        }
    }

    // Process incoming BLE data
    void ProcessIncomingBLE() {
        if (rxCharacteristic.written()) {
            auto data = rxCharacteristic.value();
            auto length = rxCharacteristic.valueLength();
            
            if (length > 0) {
                System::console << Console::debug << "BLE RX: " << std::span<const uint8>(data, length) << Console::endl;
                
                // Pass to StreamingProtocol for processing
                protocol.DataReceived(std::span<const uint8>(data, length));
            }
        }
    }

    void OnConnected() {
        // Reset protocol state on new connection
        // StreamingProtocol handles this internally
    }

    void OnDisconnected() {
        // Protocol will timeout any pending streams automatically
    }

    // Static callback for BLE characteristic written event
    static void OnRxCharacteristicWritten(BLECentral& central, BLECharacteristic& characteristic) {
        if (instance) {
            // Data processing happens in ProcessIncomingBLE()
        }
    }
};

template<size_t MaxPacketCount, size_t BufferSize>
NRF8001Communication<MaxPacketCount, BufferSize>* NRF8001Communication<MaxPacketCount, BufferSize>::instance = nullptr;
