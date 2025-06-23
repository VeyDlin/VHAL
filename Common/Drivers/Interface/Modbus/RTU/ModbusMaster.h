#pragma once

#include <System/System.h>
#include <array>
#include <span>
#include <functional>
#include <chrono>
#include <optional>

/*
Modbus RTU Master implementation for embedded systems
Provides standardized industrial communication protocol

Basic setup:
    ModbusMaster master(1);  // Slave ID 1
    master.SetSendCallback([](std::span<const uint8> data) {
        uart.SendData(data.data(), data.size());
    });
    master.SetReceiveCallback([](std::span<uint8> buffer) -> size_t {
        return uart.ReceiveData(buffer.data(), buffer.size());
    });

Reading data:
    std::array<bool, 16> coils{};
    if (master.ReadCoils(100, std::span(coils))) {
        // Process coil states
    }
    
    std::array<uint16, 10> registers{};
    if (master.ReadHoldingRegisters(200, std::span(registers))) {
        // Process register values
    }

Writing data:
    master.WriteSingleCoil(50, true);
    master.WriteSingleRegister(300, 1234);
    
    std::array<bool, 8> coilStates = {true, false, true, false...};
    master.WriteMultipleCoils(60, std::span(coilStates));
    
    std::array<uint16, 5> values = {100, 200, 300, 400, 500};
    master.WriteMultipleRegisters(400, std::span(values));

Sensor reading:
    std::array<uint16, 4> sensorData{};
    if (master.ReadInputRegisters(0, std::span(sensorData))) {
        float temperature = sensorData[0] / 10.0f;
        float humidity = sensorData[1] / 10.0f;
    }

PLC communication:
    ModbusMaster plc(1);
    // Read status from PLC
    std::array<bool, 32> status{};
    plc.ReadDiscreteInputs(0, std::span(status));
    
    // Send commands to PLC
    std::array<uint16, 8> commands = {...};
    plc.WriteMultipleRegisters(100, std::span(commands));

Error handling:
    auto result = master.ReadHoldingRegisters(100, std::span(data));
    if (result == ModbusError::Timeout) {
        // Handle communication timeout
    } else if (result == ModbusError::CrcError) {
        // Handle data corruption
    }
*/


class ModbusMaster {
public:
	enum class ModbusFunction : uint8 {
	    ReadCoils = 0x01,
	    ReadDiscreteInputs = 0x02,
	    ReadHoldingRegisters = 0x03,
	    ReadInputRegisters = 0x04,
	    WriteSingleCoil = 0x05,
	    WriteSingleRegister = 0x06,
	    WriteMultipleCoils = 0x0F,
	    WriteMultipleRegisters = 0x10
	};

	enum class ModbusError : uint8 {
	    Success = 0,
	    Timeout,
	    CrcError,
	    ExceptionResponse,
	    InvalidResponse,
	    BufferTooSmall
	};

private:
    static constexpr size_t MAX_FRAME_SIZE = 256;
    static constexpr uint16 CRC_INIT = 0xFFFF;
    static constexpr uint32 DEFAULT_TIMEOUT_MS = 1000;
    
    uint8 slaveId;
    uint32 timeoutMs;
    std::function<void(std::span<const uint8>)> sendCallback;
    std::function<size_t(std::span<uint8>)> receiveCallback;
    std::function<uint32()> getTickCallback;
    
    uint16 CalculateCrc(std::span<const uint8> data) const {
        uint16 crc = CRC_INIT;
        for (uint8 byte : data) {
            crc ^= byte;
            for (int i = 0; i < 8; ++i) {
                crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
            }
        }
        return crc;
    }
    
    bool ValidateResponse(std::span<const uint8> response, uint8 expectedFunction) const {
        if (response.size() < 3) return false;
        if (response[0] != slaveId) return false;
        if ((response[1] & 0x7F) != expectedFunction) return false;
        
        // Check CRC
        uint16 receivedCrc = (response[response.size() - 1] << 8) | response[response.size() - 2];
        uint16 calculatedCrc = CalculateCrc(response.subspan(0, response.size() - 2));
        return receivedCrc == calculatedCrc;
    }
    
    ModbusError SendRequest(std::span<const uint8> request, std::span<uint8> response, size_t& responseLength) {
        if (!sendCallback || !receiveCallback) {
            return ModbusError::InvalidResponse;
        }
        
        sendCallback(request);
        
        uint32 startTime = getTickCallback();
        while ((getTickCallback() - startTime) < timeoutMs) {
            responseLength = receiveCallback(response);
            if (responseLength > 0) {
                return ModbusError::Success;
            }
        }
        
        return ModbusError::Timeout;
    }

public:
    explicit ModbusMaster(uint8 slaveId, uint32 timeoutMs = DEFAULT_TIMEOUT_MS) 
        : slaveId(slaveId), timeoutMs(timeoutMs) {
        getTickCallback = []() -> uint32 { return System::GetTick(); };
    }
    
    void SetSendCallback(std::function<void(std::span<const uint8>)> callback) {
        sendCallback = callback;
    }
    
    void SetReceiveCallback(std::function<size_t(std::span<uint8>)> callback) {
        receiveCallback = callback;
    }
    
    void SetTickCallback(std::function<uint32()> callback) {
        getTickCallback = callback;
    }
    
    void SetTimeout(uint32 timeoutMs) {
        this->timeoutMs = timeoutMs;
    }
    
    void SetSlaveId(uint8 slaveId) {
        this->slaveId = slaveId;
    }
    
    ModbusError ReadCoils(uint16 startAddress, std::span<bool> coils) {
        if (coils.size() > 2000) return ModbusError::BufferTooSmall;
        
        std::array<uint8, 8> request{};
        request[0] = slaveId;
        request[1] = static_cast<uint8>(ModbusFunction::ReadCoils);
        request[2] = startAddress >> 8;
        request[3] = startAddress & 0xFF;
        request[4] = coils.size() >> 8;
        request[5] = coils.size() & 0xFF;
        
        uint16 crc = CalculateCrc(std::span(request.data(), 6));
        request[6] = crc & 0xFF;
        request[7] = crc >> 8;
        
        std::array<uint8, MAX_FRAME_SIZE> response{};
        size_t responseLength;
        auto error = SendRequest(std::span(request), std::span(response), responseLength);
        if (error != ModbusError::Success) return error;
        
        if (!ValidateResponse(std::span(response.data(), responseLength), static_cast<uint8>(ModbusFunction::ReadCoils))) {
            return ModbusError::InvalidResponse;
        }
        
        if (response[1] & 0x80) return ModbusError::ExceptionResponse;
        
        uint8 byteCount = response[2];
        for (size_t i = 0; i < coils.size() && i < byteCount * 8; ++i) {
            coils[i] = (response[3 + i / 8] >> (i % 8)) & 1;
        }
        
        return ModbusError::Success;
    }
    
    ModbusError ReadDiscreteInputs(uint16 startAddress, std::span<bool> inputs) {
        if (inputs.size() > 2000) return ModbusError::BufferTooSmall;
        
        std::array<uint8, 8> request{};
        request[0] = slaveId;
        request[1] = static_cast<uint8>(ModbusFunction::ReadDiscreteInputs);
        request[2] = startAddress >> 8;
        request[3] = startAddress & 0xFF;
        request[4] = inputs.size() >> 8;
        request[5] = inputs.size() & 0xFF;
        
        uint16 crc = CalculateCrc(std::span(request.data(), 6));
        request[6] = crc & 0xFF;
        request[7] = crc >> 8;
        
        std::array<uint8, MAX_FRAME_SIZE> response{};
        size_t responseLength;
        auto error = SendRequest(std::span(request), std::span(response), responseLength);
        if (error != ModbusError::Success) return error;
        
        if (!ValidateResponse(std::span(response.data(), responseLength), static_cast<uint8>(ModbusFunction::ReadDiscreteInputs))) {
            return ModbusError::InvalidResponse;
        }
        
        if (response[1] & 0x80) return ModbusError::ExceptionResponse;
        
        uint8 byteCount = response[2];
        for (size_t i = 0; i < inputs.size() && i < byteCount * 8; ++i) {
            inputs[i] = (response[3 + i / 8] >> (i % 8)) & 1;
        }
        
        return ModbusError::Success;
    }
    
    ModbusError ReadHoldingRegisters(uint16 startAddress, std::span<uint16> registers) {
        if (registers.size() > 125) return ModbusError::BufferTooSmall;
        
        std::array<uint8, 8> request{};
        request[0] = slaveId;
        request[1] = static_cast<uint8>(ModbusFunction::ReadHoldingRegisters);
        request[2] = startAddress >> 8;
        request[3] = startAddress & 0xFF;
        request[4] = registers.size() >> 8;
        request[5] = registers.size() & 0xFF;
        
        uint16 crc = CalculateCrc(std::span(request.data(), 6));
        request[6] = crc & 0xFF;
        request[7] = crc >> 8;
        
        std::array<uint8, MAX_FRAME_SIZE> response{};
        size_t responseLength;
        auto error = SendRequest(std::span(request), std::span(response), responseLength);
        if (error != ModbusError::Success) return error;
        
        if (!ValidateResponse(std::span(response.data(), responseLength), static_cast<uint8>(ModbusFunction::ReadHoldingRegisters))) {
            return ModbusError::InvalidResponse;
        }
        
        if (response[1] & 0x80) return ModbusError::ExceptionResponse;
        
        uint8 byteCount = response[2];
        for (size_t i = 0; i < registers.size() && i < byteCount / 2; ++i) {
            registers[i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
        }
        
        return ModbusError::Success;
    }
    
    ModbusError ReadInputRegisters(uint16 startAddress, std::span<uint16> registers) {
        if (registers.size() > 125) return ModbusError::BufferTooSmall;
        
        std::array<uint8, 8> request{};
        request[0] = slaveId;
        request[1] = static_cast<uint8>(ModbusFunction::ReadInputRegisters);
        request[2] = startAddress >> 8;
        request[3] = startAddress & 0xFF;
        request[4] = registers.size() >> 8;
        request[5] = registers.size() & 0xFF;
        
        uint16 crc = CalculateCrc(std::span(request.data(), 6));
        request[6] = crc & 0xFF;
        request[7] = crc >> 8;
        
        std::array<uint8, MAX_FRAME_SIZE> response{};
        size_t responseLength;
        auto error = SendRequest(std::span(request), std::span(response), responseLength);
        if (error != ModbusError::Success) return error;
        
        if (!ValidateResponse(std::span(response.data(), responseLength), static_cast<uint8>(ModbusFunction::ReadInputRegisters))) {
            return ModbusError::InvalidResponse;
        }
        
        if (response[1] & 0x80) return ModbusError::ExceptionResponse;
        
        uint8 byteCount = response[2];
        for (size_t i = 0; i < registers.size() && i < byteCount / 2; ++i) {
            registers[i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
        }
        
        return ModbusError::Success;
    }
    
    ModbusError WriteSingleCoil(uint16 address, bool value) {
        std::array<uint8, 8> request{};
        request[0] = slaveId;
        request[1] = static_cast<uint8>(ModbusFunction::WriteSingleCoil);
        request[2] = address >> 8;
        request[3] = address & 0xFF;
        request[4] = value ? 0xFF : 0x00;
        request[5] = 0x00;
        
        uint16 crc = CalculateCrc(std::span(request.data(), 6));
        request[6] = crc & 0xFF;
        request[7] = crc >> 8;
        
        std::array<uint8, MAX_FRAME_SIZE> response{};
        size_t responseLength;
        auto error = SendRequest(std::span(request), std::span(response), responseLength);
        if (error != ModbusError::Success) return error;
        
        if (!ValidateResponse(std::span(response.data(), responseLength), static_cast<uint8>(ModbusFunction::WriteSingleCoil))) {
            return ModbusError::InvalidResponse;
        }
        
        if (response[1] & 0x80) return ModbusError::ExceptionResponse;
        
        return ModbusError::Success;
    }
    
    ModbusError WriteSingleRegister(uint16 address, uint16 value) {
        std::array<uint8, 8> request{};
        request[0] = slaveId;
        request[1] = static_cast<uint8>(ModbusFunction::WriteSingleRegister);
        request[2] = address >> 8;
        request[3] = address & 0xFF;
        request[4] = value >> 8;
        request[5] = value & 0xFF;
        
        uint16 crc = CalculateCrc(std::span(request.data(), 6));
        request[6] = crc & 0xFF;
        request[7] = crc >> 8;
        
        std::array<uint8, MAX_FRAME_SIZE> response{};
        size_t responseLength;
        auto error = SendRequest(std::span(request), std::span(response), responseLength);
        if (error != ModbusError::Success) return error;
        
        if (!ValidateResponse(std::span(response.data(), responseLength), static_cast<uint8>(ModbusFunction::WriteSingleRegister))) {
            return ModbusError::InvalidResponse;
        }
        
        if (response[1] & 0x80) return ModbusError::ExceptionResponse;
        
        return ModbusError::Success;
    }
    
    ModbusError WriteMultipleCoils(uint16 startAddress, std::span<const bool> coils) {
        if (coils.size() > 1968) return ModbusError::BufferTooSmall;
        
        uint8 byteCount = (coils.size() + 7) / 8;
        std::array<uint8, MAX_FRAME_SIZE> request{};
        
        request[0] = slaveId;
        request[1] = static_cast<uint8>(ModbusFunction::WriteMultipleCoils);
        request[2] = startAddress >> 8;
        request[3] = startAddress & 0xFF;
        request[4] = coils.size() >> 8;
        request[5] = coils.size() & 0xFF;
        request[6] = byteCount;
        
        // Pack coils into bytes
        for (size_t i = 0; i < coils.size(); ++i) {
            if (coils[i]) {
                request[7 + i / 8] |= (1 << (i % 8));
            }
        }
        
        size_t requestLength = 7 + byteCount;
        uint16 crc = CalculateCrc(std::span(request.data(), requestLength));
        request[requestLength] = crc & 0xFF;
        request[requestLength + 1] = crc >> 8;
        requestLength += 2;
        
        std::array<uint8, MAX_FRAME_SIZE> response{};
        size_t responseLength;
        auto error = SendRequest(std::span(request.data(), requestLength), std::span(response), responseLength);
        if (error != ModbusError::Success) return error;
        
        if (!ValidateResponse(std::span(response.data(), responseLength), static_cast<uint8>(ModbusFunction::WriteMultipleCoils))) {
            return ModbusError::InvalidResponse;
        }
        
        if (response[1] & 0x80) return ModbusError::ExceptionResponse;
        
        return ModbusError::Success;
    }
    
    ModbusError WriteMultipleRegisters(uint16 startAddress, std::span<const uint16> registers) {
        if (registers.size() > 123) return ModbusError::BufferTooSmall;
        
        std::array<uint8, MAX_FRAME_SIZE> request{};
        
        request[0] = slaveId;
        request[1] = static_cast<uint8>(ModbusFunction::WriteMultipleRegisters);
        request[2] = startAddress >> 8;
        request[3] = startAddress & 0xFF;
        request[4] = registers.size() >> 8;
        request[5] = registers.size() & 0xFF;
        request[6] = registers.size() * 2;
        
        // Pack registers
        for (size_t i = 0; i < registers.size(); ++i) {
            request[7 + i * 2] = registers[i] >> 8;
            request[8 + i * 2] = registers[i] & 0xFF;
        }
        
        size_t requestLength = 7 + registers.size() * 2;
        uint16 crc = CalculateCrc(std::span(request.data(), requestLength));
        request[requestLength] = crc & 0xFF;
        request[requestLength + 1] = crc >> 8;
        requestLength += 2;
        
        std::array<uint8, MAX_FRAME_SIZE> response{};
        size_t responseLength;
        auto error = SendRequest(std::span(request.data(), requestLength), std::span(response), responseLength);
        if (error != ModbusError::Success) return error;
        
        if (!ValidateResponse(std::span(response.data(), responseLength), static_cast<uint8>(ModbusFunction::WriteMultipleRegisters))) {
            return ModbusError::InvalidResponse;
        }
        
        if (response[1] & 0x80) return ModbusError::ExceptionResponse;
        
        return ModbusError::Success;
    }
};
