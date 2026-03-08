#pragma once
#include <VHAL.h>
#include <functional>
#include <span>


// Abstract interface for asynchronous communication (UART, BLE, USB, SPI, etc)
class ICommunication {
public:
    using DataReceivedCallback = std::function<void(std::span<const uint8>)>;
    
    virtual ~ICommunication() = default;
    
    virtual ResultStatus Initialize() = 0;
    

    virtual ResultStatus SendData(std::span<const uint8> data) = 0;
    

    virtual bool IsReadyToSend() const = 0;
    

    virtual bool IsConnected() const = 0;
    

    virtual void SetDataReceivedCallback(DataReceivedCallback callback) = 0;
    

    virtual size_t GetMaxPacketSize() const = 0;


    virtual void Process() {
        // Default implementation does nothing
    }
};
