#pragma once
#include "../ICommunication.h"
#include <Utilities/Buffer/RingBuffer.h>
#include <Drivers/Wireless/NRF8001/BLEPeripheral.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Data/ByteConverter.h>
#include <array>

#include <Utilities/Console/Console.h>
// ICommunication implementation for NRF8001 BLE chip
// PURPOSE: concrete transport implementation via Bluetooth Low Energy
// FEATURES:
// - Uses polling for SPI queries (not interrupts)
// - Automatically splits data into BLE packets (20 bytes each)
// - Accumulates incoming packets in ring buffer
// - Requires Process() call every few milliseconds
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
    
    bool connected = false;
    bool centralInit = false;
    DataReceivedCallback dataCallback;
    
    static constexpr size_t BLE_PACKET_SIZE = BLE_ATTRIBUTE_MAX_VALUE_LENGTH;  // Standard MTU for BLE
    static constexpr uint32 POLL_INTERVAL_MS = 100;  // Poll interval
    
    uint32 lastPollTime = 0;
    
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
        	ProcessTxBuffer();
        }
    }



    virtual Status::statusType SendData(std::span<const uint8> data) override {
        if (!connected) {
            return Status::notReady;
        }
        
        if (txBuffer.GetFreeSpace() < data.size()) {
            return Status::noBufferSpaceAvailable;
        }
        
        for (uint8 byte : data) {
            txBuffer.Push(byte);
        }
        
        return Status::ok;
    }



    virtual bool IsReadyToSend() const override {
        return connected && (txBuffer.GetFreeSpace() >= BLE_PACKET_SIZE);
    }



    virtual bool IsConnected() const override {
        return connected;
    }



    virtual void SetDataReceivedCallback(DataReceivedCallback callback) override {
        dataCallback = callback;
    }



    virtual size_t GetMaxPacketSize() const override {
        return BLE_PACKET_SIZE;
    }

private:
    void OnConnected() {
        txBuffer.Clear();
    }
    


    void OnDisconnected() {
        txBuffer.Clear();
    }
    


    static void RxCharacteristicWritten(BLECentral& central, BLECharacteristic& characteristic) {
        if (instance && instance->dataCallback) {
        	auto data = std::span<const uint8>(characteristic.value(), characteristic.valueLength());
        	System::console << Console::debug << "RX: " << data << Console::endl;
            instance->dataCallback(data);
        }
    }
    
    

    void SetDeviceName(const char* name) {
        deviceName = name;
    }
    
    

    const char* GetDeviceName() const {
        return deviceName;
    }



    void ProcessTxBuffer() {
        if (txBuffer.IsEmpty() || !txCharacteristic.canNotify()) {
            return;
        }
        
        uint32 now = System::GetMs();
        if (now - lastPollTime < POLL_INTERVAL_MS) {
            return;
        }
        lastPollTime = now;

        std::array<uint8, BLE_PACKET_SIZE> packet;
        size_t packetSize = 0;
        
        while (!txBuffer.IsEmpty() && packetSize < BLE_PACKET_SIZE) {
            auto result = txBuffer.Pop();
            if (result.IsOk()) {
                packet[packetSize++] = result.data;
            }
        }
        
        if (packetSize > 0) {
            auto tx = txCharacteristic.setValue(packet.data(), packetSize);
            System::console << Console::debug << "TX(" << tx << "): " << Console::hex(packet.data(), packetSize) << Console::endl;
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
};


using NRF8001CommunicationDefault = NRF8001Communication<>;
using NRF8001CommunicationSmall = NRF8001Communication<256>;
using NRF8001CommunicationLarge = NRF8001Communication<1024>;

template<size_t BufferSize>
NRF8001Communication<BufferSize>* NRF8001Communication<BufferSize>::instance = nullptr;


