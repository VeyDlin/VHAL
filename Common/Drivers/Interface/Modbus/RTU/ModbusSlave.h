#pragma once

#include <System/System.h>
#include <array>
#include <span>
#include <functional>
#include <optional>

/*
Modbus RTU Slave implementation for embedded systems
Responds to Modbus master requests and manages device data

Basic setup:
    ModbusSlave slave(1);  // Slave ID 1
    slave.SetSendCallback([](std::span<const uint8> data) {
        uart.SendData(data.data(), data.size());
    });

Data mapping:
    std::array<bool, 100> coilData{};
    std::array<bool, 50> inputData{};
    std::array<uint16, 200> holdingRegs{};
    std::array<uint16, 100> inputRegs{};
    
    slave.SetCoilData(std::span(coilData));
    slave.SetDiscreteInputData(std::span(inputData));
    slave.SetHoldingRegisterData(std::span(holdingRegs));
    slave.SetInputRegisterData(std::span(inputRegs));

Request processing:
    void OnModbusData(std::span<const uint8> receivedData) {
        slave.ProcessRequest(receivedData);
    }

Device control:
    slave.SetCoilWriteCallback([](uint16 address, bool value) -> bool {
        if (address == 100) {
            SetOutputPin(value);
            return true;  // Success
        }
        return false;  // Address not supported
    });
    
    slave.SetRegisterWriteCallback([](uint16 address, uint16 value) -> bool {
        if (address >= 200 && address < 210) {
            SetDeviceParameter(address - 200, value);
            return true;
        }
        return false;
    });

Sensor data provider:
    ModbusSlave sensorSlave(2);
    std::array<uint16, 10> sensorValues{};
    
    // Update sensor data periodically
    void UpdateSensors() {
        sensorValues[0] = GetTemperature() * 10;  // 0.1°C resolution
        sensorValues[1] = GetHumidity() * 10;     // 0.1% resolution
        sensorValues[2] = GetPressure();          // hPa
    }

Motor controller:
    ModbusSlave motorController(3);
    motorController.SetRegisterWriteCallback([](uint16 addr, uint16 value) -> bool {
        switch (addr) {
            case 100: SetMotorSpeed(value); return true;
            case 101: SetMotorDirection(value); return true;
            case 102: if (value) StartMotor(); else StopMotor(); return true;
            default: return false;
        }
    });

Address validation:
    slave.SetAddressValidator([](uint16 address, uint16 quantity, DataType type) -> bool {
        switch (type) {
            case DataType::Coils:
                return address < 1000 && (address + quantity) <= 1000;
            case DataType::HoldingRegisters:
                return address >= 100 && address < 500;
            default:
                return true;
        }
    });

Custom exception handling:
    slave.SetExceptionCallback([](ModbusException exception, uint16 address) {
        System::console << "Modbus exception: " << (int)exception 
                       << " at address: " << address << Console::endl;
    });
*/


class ModbusSlave {
public:
	enum class ModbusException : uint8 {
	    IllegalFunction = 0x01,
	    IllegalDataAddress = 0x02,
	    IllegalDataValue = 0x03,
	    SlaveDeviceFailure = 0x04
	};

	enum class DataType : uint8 {
	    Coils,
	    DiscreteInputs,
	    HoldingRegisters,
	    InputRegisters
	};

private:
    static constexpr size_t MAX_FRAME_SIZE = 256;
    static constexpr uint16 CRC_INIT = 0xFFFF;
    
    uint8 slaveId;
    std::function<void(std::span<const uint8>)> sendCallback;
    std::function<bool(uint16, bool)> coilWriteCallback;
    std::function<bool(uint16, uint16)> registerWriteCallback;
    std::function<bool(uint16, uint16, DataType)> addressValidator;
    std::function<void(ModbusException, uint16)> exceptionCallback;
    
    std::span<bool> coilData;
    std::span<bool> discreteInputData;
    std::span<uint16> holdingRegisterData;
    std::span<uint16> inputRegisterData;
    
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
    
    bool ValidateRequest(std::span<const uint8> request) const {
        if (request.size() < 4) return false;
        
        // Check CRC
        uint16 receivedCrc = (request[request.size() - 1] << 8) | request[request.size() - 2];
        uint16 calculatedCrc = CalculateCrc(request.subspan(0, request.size() - 2));
        return receivedCrc == calculatedCrc;
    }
    
    void SendExceptionResponse(uint8 functionCode, ModbusException exception) {
        if (!sendCallback) return;
        
        std::array<uint8, 5> response{};
        response[0] = slaveId;
        response[1] = functionCode | 0x80;  // Set exception bit
        response[2] = static_cast<uint8>(exception);
        
        uint16 crc = CalculateCrc(std::span(response.data(), 3));
        response[3] = crc & 0xFF;
        response[4] = crc >> 8;
        
        sendCallback(std::span(response));
        
        if (exceptionCallback) {
            exceptionCallback(exception, 0);
        }
    }

public:
    explicit ModbusSlave(uint8 slaveId) : slaveId(slaveId) {}
    
    void SetSendCallback(std::function<void(std::span<const uint8>)> callback) {
        sendCallback = callback;
    }
    
    void SetCoilWriteCallback(std::function<bool(uint16, bool)> callback) {
        coilWriteCallback = callback;
    }
    
    void SetRegisterWriteCallback(std::function<bool(uint16, uint16)> callback) {
        registerWriteCallback = callback;
    }
    
    void SetAddressValidator(std::function<bool(uint16, uint16, DataType)> validator) {
        addressValidator = validator;
    }
    
    void SetExceptionCallback(std::function<void(ModbusException, uint16)> callback) {
        exceptionCallback = callback;
    }
    
    void SetCoilData(std::span<bool> data) {
        coilData = data;
    }
    
    void SetDiscreteInputData(std::span<bool> data) {
        discreteInputData = data;
    }
    
    void SetHoldingRegisterData(std::span<uint16> data) {
        holdingRegisterData = data;
    }
    
    void SetInputRegisterData(std::span<uint16> data) {
        inputRegisterData = data;
    }
    
    void ProcessRequest(std::span<const uint8> request) {
        if (request.size() < 4) return;
        
        uint8 receivedSlaveId = request[0];
        uint8 functionCode = request[1];
        
        // Ignore if not for us (unless broadcast)
        if (receivedSlaveId != slaveId && receivedSlaveId != 0) {
            return;
        }
        
        if (!ValidateRequest(request)) {
            return; // Invalid CRC, ignore silently
        }
        
        switch (static_cast<ModbusFunction>(functionCode)) {
            case ModbusFunction::ReadCoils:
                HandleReadCoils(request);
                break;
            case ModbusFunction::ReadDiscreteInputs:
                HandleReadDiscreteInputs(request);
                break;
            case ModbusFunction::ReadHoldingRegisters:
                HandleReadHoldingRegisters(request);
                break;
            case ModbusFunction::ReadInputRegisters:
                HandleReadInputRegisters(request);
                break;
            case ModbusFunction::WriteSingleCoil:
                HandleWriteSingleCoil(request);
                break;
            case ModbusFunction::WriteSingleRegister:
                HandleWriteSingleRegister(request);
                break;
            case ModbusFunction::WriteMultipleCoils:
                HandleWriteMultipleCoils(request);
                break;
            case ModbusFunction::WriteMultipleRegisters:
                HandleWriteMultipleRegisters(request);
                break;
            default:
                SendExceptionResponse(functionCode, ModbusException::IllegalFunction);
                break;
        }
    }
    
    void SetCoil(uint16 address, bool value) {
        if (address < coilData.size()) {
            coilData[address] = value;
        }
    }
    
    bool GetCoil(uint16 address) const {
        if (address < coilData.size()) {
            return coilData[address];
        }
        return false;
    }
    
    void SetRegister(uint16 address, uint16 value) {
        if (address < holdingRegisterData.size()) {
            holdingRegisterData[address] = value;
        }
    }
    
    uint16 GetRegister(uint16 address) const {
        if (address < holdingRegisterData.size()) {
            return holdingRegisterData[address];
        }
        return 0;
    }

private:
    void HandleReadCoils(std::span<const uint8> request) {
        if (request.size() < 8) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadCoils), ModbusException::IllegalDataValue);
            return;
        }
        
        uint16 startAddress = (request[2] << 8) | request[3];
        uint16 quantity = (request[4] << 8) | request[5];
        
        if (quantity == 0 || quantity > 2000) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadCoils), ModbusException::IllegalDataValue);
            return;
        }
        
        if (addressValidator && !addressValidator(startAddress, quantity, DataType::Coils)) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadCoils), ModbusException::IllegalDataAddress);
            return;
        }
        
        if (startAddress + quantity > coilData.size()) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadCoils), ModbusException::IllegalDataAddress);
            return;
        }
        
        uint8 byteCount = (quantity + 7) / 8;
        std::array<uint8, MAX_FRAME_SIZE> response{};
        
        response[0] = slaveId;
        response[1] = static_cast<uint8>(ModbusFunction::ReadCoils);
        response[2] = byteCount;
        
        // Pack coils into bytes
        for (uint16 i = 0; i < quantity; ++i) {
            if (coilData[startAddress + i]) {
                response[3 + i / 8] |= (1 << (i % 8));
            }
        }
        
        size_t responseLength = 3 + byteCount;
        uint16 crc = CalculateCrc(std::span(response.data(), responseLength));
        response[responseLength] = crc & 0xFF;
        response[responseLength + 1] = crc >> 8;
        responseLength += 2;
        
        if (sendCallback) {
            sendCallback(std::span(response.data(), responseLength));
        }
    }
    
    void HandleReadDiscreteInputs(std::span<const uint8> request) {
        if (request.size() < 8) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadDiscreteInputs), ModbusException::IllegalDataValue);
            return;
        }
        
        uint16 startAddress = (request[2] << 8) | request[3];
        uint16 quantity = (request[4] << 8) | request[5];
        
        if (quantity == 0 || quantity > 2000) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadDiscreteInputs), ModbusException::IllegalDataValue);
            return;
        }
        
        if (addressValidator && !addressValidator(startAddress, quantity, DataType::DiscreteInputs)) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadDiscreteInputs), ModbusException::IllegalDataAddress);
            return;
        }
        
        if (startAddress + quantity > discreteInputData.size()) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadDiscreteInputs), ModbusException::IllegalDataAddress);
            return;
        }
        
        uint8 byteCount = (quantity + 7) / 8;
        std::array<uint8, MAX_FRAME_SIZE> response{};
        
        response[0] = slaveId;
        response[1] = static_cast<uint8>(ModbusFunction::ReadDiscreteInputs);
        response[2] = byteCount;
        
        // Pack inputs into bytes
        for (uint16 i = 0; i < quantity; ++i) {
            if (discreteInputData[startAddress + i]) {
                response[3 + i / 8] |= (1 << (i % 8));
            }
        }
        
        size_t responseLength = 3 + byteCount;
        uint16 crc = CalculateCrc(std::span(response.data(), responseLength));
        response[responseLength] = crc & 0xFF;
        response[responseLength + 1] = crc >> 8;
        responseLength += 2;
        
        if (sendCallback) {
            sendCallback(std::span(response.data(), responseLength));
        }
    }
    
    void HandleReadHoldingRegisters(std::span<const uint8> request) {
        if (request.size() < 8) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadHoldingRegisters), ModbusException::IllegalDataValue);
            return;
        }
        
        uint16 startAddress = (request[2] << 8) | request[3];
        uint16 quantity = (request[4] << 8) | request[5];
        
        if (quantity == 0 || quantity > 125) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadHoldingRegisters), ModbusException::IllegalDataValue);
            return;
        }
        
        if (addressValidator && !addressValidator(startAddress, quantity, DataType::HoldingRegisters)) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadHoldingRegisters), ModbusException::IllegalDataAddress);
            return;
        }
        
        if (startAddress + quantity > holdingRegisterData.size()) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadHoldingRegisters), ModbusException::IllegalDataAddress);
            return;
        }
        
        std::array<uint8, MAX_FRAME_SIZE> response{};
        
        response[0] = slaveId;
        response[1] = static_cast<uint8>(ModbusFunction::ReadHoldingRegisters);
        response[2] = quantity * 2;
        
        // Pack registers
        for (uint16 i = 0; i < quantity; ++i) {
            uint16 value = holdingRegisterData[startAddress + i];
            response[3 + i * 2] = value >> 8;
            response[4 + i * 2] = value & 0xFF;
        }
        
        size_t responseLength = 3 + quantity * 2;
        uint16 crc = CalculateCrc(std::span(response.data(), responseLength));
        response[responseLength] = crc & 0xFF;
        response[responseLength + 1] = crc >> 8;
        responseLength += 2;
        
        if (sendCallback) {
            sendCallback(std::span(response.data(), responseLength));
        }
    }
    
    void HandleReadInputRegisters(std::span<const uint8> request) {
        if (request.size() < 8) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadInputRegisters), ModbusException::IllegalDataValue);
            return;
        }
        
        uint16 startAddress = (request[2] << 8) | request[3];
        uint16 quantity = (request[4] << 8) | request[5];
        
        if (quantity == 0 || quantity > 125) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadInputRegisters), ModbusException::IllegalDataValue);
            return;
        }
        
        if (addressValidator && !addressValidator(startAddress, quantity, DataType::InputRegisters)) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadInputRegisters), ModbusException::IllegalDataAddress);
            return;
        }
        
        if (startAddress + quantity > inputRegisterData.size()) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::ReadInputRegisters), ModbusException::IllegalDataAddress);
            return;
        }
        
        std::array<uint8, MAX_FRAME_SIZE> response{};
        
        response[0] = slaveId;
        response[1] = static_cast<uint8>(ModbusFunction::ReadInputRegisters);
        response[2] = quantity * 2;
        
        // Pack registers
        for (uint16 i = 0; i < quantity; ++i) {
            uint16 value = inputRegisterData[startAddress + i];
            response[3 + i * 2] = value >> 8;
            response[4 + i * 2] = value & 0xFF;
        }
        
        size_t responseLength = 3 + quantity * 2;
        uint16 crc = CalculateCrc(std::span(response.data(), responseLength));
        response[responseLength] = crc & 0xFF;
        response[responseLength + 1] = crc >> 8;
        responseLength += 2;
        
        if (sendCallback) {
            sendCallback(std::span(response.data(), responseLength));
        }
    }
    
    void HandleWriteSingleCoil(std::span<const uint8> request) {
        if (request.size() < 8) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteSingleCoil), ModbusException::IllegalDataValue);
            return;
        }
        
        uint16 address = (request[2] << 8) | request[3];
        uint16 value = (request[4] << 8) | request[5];
        
        if (value != 0x0000 && value != 0xFF00) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteSingleCoil), ModbusException::IllegalDataValue);
            return;
        }
        
        bool coilValue = (value == 0xFF00);
        
        if (addressValidator && !addressValidator(address, 1, DataType::Coils)) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteSingleCoil), ModbusException::IllegalDataAddress);
            return;
        }
        
        if (coilWriteCallback) {
            if (!coilWriteCallback(address, coilValue)) {
                SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteSingleCoil), ModbusException::SlaveDeviceFailure);
                return;
            }
        } else if (address < coilData.size()) {
            coilData[address] = coilValue;
        } else {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteSingleCoil), ModbusException::IllegalDataAddress);
            return;
        }
        
        // Echo back the request
        if (sendCallback) {
            sendCallback(request);
        }
    }
    
    void HandleWriteSingleRegister(std::span<const uint8> request) {
        if (request.size() < 8) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteSingleRegister), ModbusException::IllegalDataValue);
            return;
        }
        
        uint16 address = (request[2] << 8) | request[3];
        uint16 value = (request[4] << 8) | request[5];
        
        if (addressValidator && !addressValidator(address, 1, DataType::HoldingRegisters)) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteSingleRegister), ModbusException::IllegalDataAddress);
            return;
        }
        
        if (registerWriteCallback) {
            if (!registerWriteCallback(address, value)) {
                SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteSingleRegister), ModbusException::SlaveDeviceFailure);
                return;
            }
        } else if (address < holdingRegisterData.size()) {
            holdingRegisterData[address] = value;
        } else {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteSingleRegister), ModbusException::IllegalDataAddress);
            return;
        }
        
        // Echo back the request
        if (sendCallback) {
            sendCallback(request);
        }
    }
    
    void HandleWriteMultipleCoils(std::span<const uint8> request) {
        if (request.size() < 9) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleCoils), ModbusException::IllegalDataValue);
            return;
        }
        
        uint16 startAddress = (request[2] << 8) | request[3];
        uint16 quantity = (request[4] << 8) | request[5];
        uint8 byteCount = request[6];
        
        if (quantity == 0 || quantity > 1968 || byteCount != (quantity + 7) / 8) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleCoils), ModbusException::IllegalDataValue);
            return;
        }
        
        if (request.size() < 9 + byteCount) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleCoils), ModbusException::IllegalDataValue);
            return;
        }
        
        if (addressValidator && !addressValidator(startAddress, quantity, DataType::Coils)) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleCoils), ModbusException::IllegalDataAddress);
            return;
        }
        
        // Write coils
        for (uint16 i = 0; i < quantity; ++i) {
            bool coilValue = (request[7 + i / 8] >> (i % 8)) & 1;
            
            if (coilWriteCallback) {
                if (!coilWriteCallback(startAddress + i, coilValue)) {
                    SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleCoils), ModbusException::SlaveDeviceFailure);
                    return;
                }
            } else if (startAddress + i < coilData.size()) {
                coilData[startAddress + i] = coilValue;
            } else {
                SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleCoils), ModbusException::IllegalDataAddress);
                return;
            }
        }
        
        // Send response
        std::array<uint8, 8> response{};
        response[0] = slaveId;
        response[1] = static_cast<uint8>(ModbusFunction::WriteMultipleCoils);
        response[2] = startAddress >> 8;
        response[3] = startAddress & 0xFF;
        response[4] = quantity >> 8;
        response[5] = quantity & 0xFF;
        
        uint16 crc = CalculateCrc(std::span(response.data(), 6));
        response[6] = crc & 0xFF;
        response[7] = crc >> 8;
        
        if (sendCallback) {
            sendCallback(std::span(response));
        }
    }
    
    void HandleWriteMultipleRegisters(std::span<const uint8> request) {
        if (request.size() < 9) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleRegisters), ModbusException::IllegalDataValue);
            return;
        }
        
        uint16 startAddress = (request[2] << 8) | request[3];
        uint16 quantity = (request[4] << 8) | request[5];
        uint8 byteCount = request[6];
        
        if (quantity == 0 || quantity > 123 || byteCount != quantity * 2) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleRegisters), ModbusException::IllegalDataValue);
            return;
        }
        
        if (request.size() < 9 + byteCount) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleRegisters), ModbusException::IllegalDataValue);
            return;
        }
        
        if (addressValidator && !addressValidator(startAddress, quantity, DataType::HoldingRegisters)) {
            SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleRegisters), ModbusException::IllegalDataAddress);
            return;
        }
        
        // Write registers
        for (uint16 i = 0; i < quantity; ++i) {
            uint16 value = (request[7 + i * 2] << 8) | request[8 + i * 2];
            
            if (registerWriteCallback) {
                if (!registerWriteCallback(startAddress + i, value)) {
                    SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleRegisters), ModbusException::SlaveDeviceFailure);
                    return;
                }
            } else if (startAddress + i < holdingRegisterData.size()) {
                holdingRegisterData[startAddress + i] = value;
            } else {
                SendExceptionResponse(static_cast<uint8>(ModbusFunction::WriteMultipleRegisters), ModbusException::IllegalDataAddress);
                return;
            }
        }
        
        // Send response
        std::array<uint8, 8> response{};
        response[0] = slaveId;
        response[1] = static_cast<uint8>(ModbusFunction::WriteMultipleRegisters);
        response[2] = startAddress >> 8;
        response[3] = startAddress & 0xFF;
        response[4] = quantity >> 8;
        response[5] = quantity & 0xFF;
        
        uint16 crc = CalculateCrc(std::span(response.data(), 6));
        response[6] = crc & 0xFF;
        response[7] = crc >> 8;
        
        if (sendCallback) {
            sendCallback(std::span(response));
        }
    }
};
