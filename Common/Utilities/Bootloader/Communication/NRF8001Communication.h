#pragma once
#include "../ICommunication.h"
#include <Drivers/Wireless/NRF8001/BLEPeripheral.h>
#include <Drivers/Interface/User/StreamingProtocol/CompactStreamingProtocol.h>
#include <Utilities/Data/ByteConverter.h>
#include <array>

// Simplified NRF8001Communication using CompactStreamingProtocol
// This uses the optimized protocol for BLE constraints (4-byte header vs 13-byte)
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
    
    // CompactStreamingProtocol handles all the complexity with optimized headers!
    CompactStreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount> protocol;
    
    bool connected = false;
    DataReceivedCallback dataCallback;
    
    // TX buffer for storing data during transmission
    std::array<uint8, 256> txDataBuffer;  // Bootloader max command size + response header + encryption padding
    size_t txDataSize = 0;
    bool txInProgress = false;
    
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
            [this](uint8 fragmentIndex, std::span<const uint8> data) -> bool {
            	// TODO: мы ведь при rety росто повторно запишем данные?
            	// возможно стоит тут ранить послейдний fragmentIndex и последний пакет std::span<const uint8> data копировать,
            	// при новом пакеты записывать предыдущий а при Stream complete записывать последний,
            	// при этом ничего не записывать если индекс тот же что и предыдущий в буфере,
            	// так когда нам клиент шлет опять повторно пакет, мы не записываем записаное
                return this->HandleReceivedData(fragmentIndex, data);
            },
            // Stream complete callback - log completion
            [this](CompactStreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>::StreamResult result) -> void {
                this->HandleStreamComplete(result);
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
            System::console << Console::endl << Console::endl << Console::separator() << Console::endl;
            System::console << Console::debug << "Connected" << Console::endl;
            OnConnected();
        } else if (!connected && wasConnected) {
            System::console << Console::debug << "Disconnected" << Console::endl;
            OnDisconnected();
        }

        if (connected) {
            // Process incoming BLE data
            ProcessIncomingBLE();
            
            // Let CompactStreamingProtocol handle all the protocol logic
            protocol.Process();
        }
    }

    virtual Status::statusType SendData(std::span<const uint8> data) override {
        if (!connected) {
            return Status::notReady;
        }
        
        if (txInProgress) {
            return Status::busy;  // Предыдущая передача не завершена
        }
        
        if (data.size() > txDataBuffer.size()) {
            return Status::noBufferSpaceAvailable;
        }

        // Копируем данные в наш буфер
        std::copy(data.begin(), data.end(), txDataBuffer.begin());
        txDataSize = data.size();
        txInProgress = true;
        
        // Запускаем отправку через CompactStreamingProtocol
        auto result = protocol.Send(std::span<const uint8>(txDataBuffer.data(), txDataSize));
        
        if (result.status == CompactStreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>::SendStatus::Success) {
            // CompactStreamingProtocol принял данные, они будут отправлены асинхронно
            // txInProgress будет сброшен в HandleStreamComplete
            return Status::ok;
        } else if (result.status == CompactStreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>::SendStatus::Busy) {
            txInProgress = false;
            return Status::busy;
        } else {
            txInProgress = false;
            return Status::error;
        }
    }

    virtual void SetDataReceivedCallback(DataReceivedCallback callback) override {
        dataCallback = callback;
    }

    virtual bool IsConnected() const override {
        return connected;
    }

    virtual bool IsReadyToSend() const override {
        // Ready if connected and CompactStreamingProtocol has buffer space
        return connected;  // CompactStreamingProtocol manages its own buffers
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
        static uint32 failCount = 0;
        static uint32 successCount = 0;
        
        if (txCharacteristic.setValue(data.data(), data.size())) {
            successCount++;
            return true;
        }
        
        failCount++;
        return false;
    }

    // Handle received data from CompactStreamingProtocol
    bool HandleReceivedData(uint8 fragmentIndex, std::span<const uint8> data) {
        if (data.empty()) {
            // Пустой span = сигнал сброса состояния для bootloader
            if (dataCallback) {
                dataCallback(data);  // Передаем пустой span как сигнал сброса
            }
            return true;
        }
        
        // Передаем каждый валидный фрагмент НЕМЕДЛЕННО в bootloader
        // Это streaming протокол - bootloader обработает данные по мере поступления
        if (dataCallback) {
            dataCallback(data);
        }
        
        return true; // Always accept data
    }

    // Handle stream completion
    void HandleStreamComplete(CompactStreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>::StreamResult result) {
        if (result == CompactStreamingProtocol<BLE_PACKET_SIZE, MaxPacketCount>::StreamResult::Success) {
            // Release TX buffer if it was in progress (outgoing stream completed)
            if (txInProgress) {
                txInProgress = false;
            }
        } else {

            // Для любой ошибки потока уведомляем bootloader о сбросе состояния
            if (dataCallback) {
                dataCallback(std::span<const uint8>());
            }
            
            // Освобождаем TX буфер при ошибке, если он был занят
            if (txInProgress) {
                txInProgress = false;
            }
        }
    }

    // Process incoming BLE data
    void ProcessIncomingBLE() {
        if (rxCharacteristic.written()) {
            auto data = rxCharacteristic.value();
            auto length = rxCharacteristic.valueLength();
            
            if (length > 0) {
                // Pass to CompactStreamingProtocol for processing
                protocol.DataReceived(std::span<const uint8>(data, length));
            }
        }
    }

    void OnConnected() {
        // Reset protocol state on new connection
        protocol.Reset();  // ВАЖНО: сбрасываем состояние протокола при новом подключении!
        
        // НЕ вызываем dataCallback - не нужно сбрасывать состояние bootloader при подключении
        txInProgress = false;  // Сбрасываем флаг передачи
    }

    void OnDisconnected() {
        // Protocol will timeout any pending streams automatically
        // НЕ вызываем dataCallback - не нужно сбрасывать состояние bootloader при отключении
        txInProgress = false;  // Сбрасываем флаг передачи
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
