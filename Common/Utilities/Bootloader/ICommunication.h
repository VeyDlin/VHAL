#pragma once
#include <BSP.h>
#include <functional>
#include <span>


// Abstract interface for asynchronous communication (UART, BLE, USB, SPI, etc)
// 
// IMPLEMENTER RESPONSIBILITIES:
// 1. Implement concrete transport protocol (UART, BLE NRF8001, USB CDC, etc)
// 2. Ensure data integrity checking (CRC, checksums)
// 3. Implement retransmission on errors (if required)
// 4. Handle fragmentation for protocols with limited MTU (e.g. BLE - 20 bytes)
// 5. Call DataReceivedCallback only with valid data (that passed CRC check)
//
// IMPORTANT FOR IMPLEMENTERS:
// - Process() must be non-blocking and fast (<10ms)
// - For interrupt-based protocols (UART) Process() can be empty
// - For polling-based protocols (SPI NRF8001) Process() must poll the device
// - DataReceivedCallback can be called from interrupt or from Process()
// - GetMaxPacketSize() must return payload size WITHOUT overhead data
//
// IMPLEMENTATION EXAMPLES:
// - UARTCommunication: uses interrupts, Process() is empty
// - NRF8001Communication: polls SPI in Process(), manages BLE stack
// - USBCommunication: uses USB interrupts, Process() handles endpoints
class ICommunication {
public:
    using DataReceivedCallback = std::function<void(std::span<const uint8>)>;
    
    virtual ~ICommunication() = default;
    
    // IMPLEMENTER MUST:
    // - Initialize hardware (UART, SPI, USB, etc.)
    // - Configure interrupts if used
    // - For BLE: initialize stack, start advertising
    // - Return Status::ok only if initialization is successful
    virtual Status::statusType Initialize() = 0;
    

    // IMPLEMENTER MUST:
    // - Add protocol overhead data (headers, CRC, length)
    // - Queue for sending if cannot send immediately
    // - For BLE: split into 20-byte packets
    // - Return Status::bufferFull if queue is full
    // - Return Status::notReady if not connected
    virtual Status::statusType SendData(std::span<const uint8> data) = 0;
    

    // IMPLEMENTER MUST return true if:
    // - Connected to master
    // - Transmit buffer has space for at least one packet
    // - Hardware is ready to transmit (no active DMA transaction, etc.)
    virtual bool IsReadyToSend() const = 0;
    

    // IMPLEMENTER MUST return true if:
    // - For UART: always true (unless hardware handshake is used)
    // - For BLE: active connection with central device exists
    // - For USB: device is enumerated and endpoint is open
    virtual bool IsConnected() const = 0;
    

    // IMPLEMENTER MUST:
    // - Store the callback
    // - Call it ONLY with valid data (that passed CRC)
    // - Can be called from interrupt or from Process()
    // - Pass data as-is (without protocol headers)
    virtual void SetDataReceivedCallback(DataReceivedCallback callback) = 0;
    

    // IMPLEMENTER MUST return:
    // - Maximum PAYLOAD data size (without headers/CRC)
    // - For UART: typically 240-255 bytes
    // - For BLE 4.0: 20 bytes
    // - For BLE 4.2+: up to 244 bytes (if supported)
    // - For USB: endpoint size (typically 64 bytes)
    virtual size_t GetMaxPacketSize() const = 0;


    // IMPLEMENTER MUST (if required):
    // - For NRF8001: check RDYN pin, perform SPI transaction, process BLE events
    // - For DMA UART: check DMA completion, start new transaction
    // - For USB: process endpoints, check buffers
    // - Handle timeouts and retransmissions
    // - Call DataReceivedCallback if new data received
    //
    // IMPORTANT:
    // - Method must be NON-BLOCKING
    // - Execution time < 10ms (preferably < 5ms)
    // - For interrupt-based protocols can be left empty
    virtual void Process() {
        // Default implementation does nothing
    }
};
