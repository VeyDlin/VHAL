#pragma once
#include "../ICommunication.h"
#include <Drivers/Wireless/NRF8001/BLEPeripheral.h>
#include <Drivers/Interface/User/StreamingProtocol/CompactStreamingProtocol.h>
#include <Utilities/Data/ByteConverter.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <array>

// NRF8001 BLE communication using CompactStreamingProtocol
template<size_t MaxPacketCount = 4, size_t BufferSize = 512>
class NRF8001Communication : public ICommunication {
protected:
    static constexpr size_t BLE_PACKET_SIZE = BLE_ATTRIBUTE_MAX_VALUE_LENGTH;  // 20 bytes
    using Protocol = CompactStreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>;

    ASPI& spi;
    AGPIO& reqnPin;    // Request pin (from NRF8001)
    AGPIO& rdynPin;    // Ready pin (from NRF8001)
    AGPIO& resetPin;   // Reset pin (to NRF8001)

    BLEPeripheral ble;
    std::optional<BLECentral> central;
    
    // CompactStreamingProtocol handles all the complexity with optimized headers!
    Protocol protocol;
    
    bool connected = false;
    DataReceivedCallback dataCallback;
    
    // BLE writes are staged here by the write event handler (which runs inside the driver's poll)
    // and drained into the protocol from Process(). The protocol must not be called from the handler.
    static_assert(BufferSize <= 65535, "BufferSize must fit into RingBuffer (uint16 size)");
    RingBuffer<uint8, BufferSize> rxStaging;
    
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
        protocol(
            // Transmit callback - send data via BLE
            [this](std::span<const uint8> data) -> bool {
                return this->SendBLEPacket(data);
            },
            // Data received callback - pass to ICommunication callback
            [this](uint8 fragmentIndex, std::span<const uint8> data) -> bool {
                // Retransmitted fragments are filtered by the protocol (message id + fragment index)
                return this->HandleReceivedData(fragmentIndex, data);
            },
            // Stream completion: an abandoned or failed outgoing message lost its tail, so the bootloader
            // must drop partial input and the pending response
            [this](typename Protocol::StreamResult result) -> void {
                if (result != Protocol::StreamResult::Success) {
                    this->NotifyBootloaderFramingLost();
                }
            },
            // Receive reset: a new message started while the previous incoming one was incomplete
            [this]() -> void {
                this->NotifyBootloaderFramingLost();
            }
        ),
        deviceName(customDeviceName)
    { }

    virtual ~NRF8001Communication() = default;

    virtual ResultStatus Initialize() override {
        SetupBootloaderService();
        
        ble.setLocalName(deviceName);
        ble.setDeviceName(deviceName);
        
        instance = this;
        ble.begin();

        return ResultStatus::ok;
    }

    virtual void Process() override {
        // Handle BLE connection
        if (!central.has_value() || !*central || !central->connected()) {
            central = ble.central();
        }

        bool wasConnected = connected;
        connected = *central && central->connected();
        
        if (connected && !wasConnected) {
            OnConnected();
        } else if (!connected && wasConnected) {
            OnDisconnected();
        }

        if (connected) {
            // Drain writes staged by the BLE event handler, then run the protocol
            DrainReceivedBLE();
            
            // Let CompactStreamingProtocol handle all the protocol logic
            protocol.Process();
        }
    }

    virtual ResultStatus SendData(std::span<const uint8> data) override {
        if (!connected) {
            return ResultStatus::notReady;
        }
        
        // The protocol owns the transmit buffer and reports Busy while a previous message is in flight
        typename Protocol::SendResult result = protocol.Send(data);

        if (result.status == Protocol::SendStatus::Success) {
            return ResultStatus::ok;
        }
        
        if (result.status == Protocol::SendStatus::Busy) {
            return ResultStatus::busy;
        }

        return ResultStatus::error;
    }

    virtual void SetDataReceivedCallback(DataReceivedCallback callback) override {
        dataCallback = callback;
    }

    virtual bool IsConnected() const override {
        return connected;
    }

    virtual bool IsReadyToSend() const override {
        // Ready only when connected and the protocol is not sending a message
        return connected && !protocol.IsTransmitting();
    }

    virtual size_t GetMaxPacketSize() const override {
        return BLE_PACKET_SIZE;
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

    // BLE packet transmission - called by CompactStreamingProtocol
    bool SendBLEPacket(std::span<const uint8> data) {
        if (!connected) {
            return false;
        }
        
        if (data.size() > BLE_PACKET_SIZE) {
            return false;
        }
        
        // Send via BLE notification
        return txCharacteristic.setValue(data.data(), static_cast<uint8>(data.size()));
    }

    // Handle received data from CompactStreamingProtocol
    bool HandleReceivedData(uint8 fragmentIndex, std::span<const uint8> data) {
        if (data.empty()) {
            // Empty fragments are ACKed by the protocol and carry no data
            return true;
        }
        
        // Pass every valid fragment to the bootloader immediately (streaming protocol)
        if (dataCallback) {
            dataCallback(data);
        }
        
        return true; // Always accept data
    }

    // Moves the writes staged by the event handler into the protocol (never called from the handler)
    void DrainReceivedBLE() {
        std::array<uint8, BLE_PACKET_SIZE> chunk{};

        while (!rxStaging.IsEmpty()) {
            size_t count = 0;

            while (count < chunk.size() && !rxStaging.IsEmpty()) {
                Result<uint8> byte = rxStaging.Pop();

                if (byte.IsOk()) {
                    chunk[count] = byte.Value();
                    count++;
                }
            }
            
            protocol.DataReceived(std::span<const uint8>(chunk.data(), count));
        }
    }

    // Transport lost framing: reset the protocol and tell the bootloader to drop partial input
    void ResetTransport() {
        protocol.Reset();
        rxStaging.Clear();
        NotifyBootloaderFramingLost();
    }
            
    // The empty span means "the transport lost framing": the bootloader drops its partial input and any
    // pending response. It does not abort an erase or change the write stream (see IBootloader.h, A3).
    void NotifyBootloaderFramingLost() {
        if (dataCallback) {
            dataCallback(std::span<const uint8>());
        }
    }

    void OnConnected() {
        ResetTransport();
    }

    void OnDisconnected() {
        ResetTransport();
    }

    // Called synchronously by the BLE driver from inside its poll: only stage the bytes here
    static void OnRxCharacteristicWritten(BLECentral& central, BLECharacteristic& characteristic) {
        if (!instance) {
            return;
        }

        const uint8* value = characteristic.value();
        size_t length = characteristic.valueLength();

        if (!value) {
            return;
        }

        for (size_t i = 0; i < length; i++) {
            instance->rxStaging.Push(value[i]);
        }
    }
};

template<size_t MaxPacketCount, size_t BufferSize>
NRF8001Communication<MaxPacketCount, BufferSize>* NRF8001Communication<MaxPacketCount, BufferSize>::instance = nullptr;
