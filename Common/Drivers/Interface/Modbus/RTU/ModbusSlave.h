#pragma once
#include <BSP.h>
#include <functional>
#include <Utilities/Data/Byte/ByteConverter.h>
#include <Utilities/Data/Byte/Bit.h>
#include <cstring>


#define MODBUS_BROADCAST_ADDRESS 0

/* Protocol exceptions */
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION     1
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 2
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE   3
#define MODBUS_INFORMATIVE_NOT_FOR_US   4



#define _MODBUS_RTU_SLAVE 0
#define _MODBUS_RTU_FUNCTION 1
#define _MODBUS_RTU_PRESET_REQ_LENGTH 6
#define _MODBUS_RTU_PRESET_RSP_LENGTH 2

#define _MODBUS_RTU_CHECKSUM_LENGTH 2
#define _MODBUSINO_RTU_MAX_ADU_LENGTH 256
#define _FC_READ_HOLDING_REGISTERS 0x03
#define _FC_WRITE_MULTIPLE_REGISTERS 0x10


class ModbusSlave {
private:
	static constexpr size_t maxRxBuffer = 128;
	static constexpr size_t maxTxBuffer = 128;

	uint8 rxBuffer[maxRxBuffer] = { 0 };
	uint8 rxBufferCounter = 0;

	uint8 txBuffer[maxTxBuffer] = { 0 };

    enum class Step {
    	std::function, Meta, Data
    };

	enum class BusStatus : uint8 {
		Success = 0x00,				// success
		InvalidSlaveID = 0xE0,		// invalid response slave ID exception
		InvalidFunction = 0xE1,		// invalid response function exception
		ResponseTimedOut = 0xE2, 	// response timed out exception
		InvalidCRC = 0xE3, 			// invalid response CRC exception
		UnknownError = 0xFF
	};

	uint8 slaveId = 0;
	uint16 *registerTable = nullptr;
	uint16 registerTableSize = 0;

	AUART *serial;
	AGPIO *dePin;

public:
	uint16 rxTimeout = 100;

	std::function<bool(uint16 registerIndex, uint16 &registerVal)> onUpdateRegister;

public:
    ModbusSlave() {}

    ModbusSlave(uint8 slave, uint16 *table, uint16 size) {
    	registerTable = table;
    	registerTableSize = size;

    	if (slave >= 0 && slave <= 247) {
    		slaveId = slave;
    	}
    }





    void Initialization(AUART &uart, AUART::Parameters parameters, AGPIO &de) {
    	serial = &uart;
    	dePin = &de;

    	dePin->SetParameters({ AGPIO::Mode::Output });
    	dePin->Reset();

    	serial->SetParameters(parameters);

    	serial->SetContinuousAsyncRxMode(true);

    	serial->onInterrupt = [this](auto type) {
    		if(type == AUART::Irq::Rx) {
    			if(rxBufferCounter < maxRxBuffer) {
    				rxBuffer[rxBufferCounter++] = serial->GetLastRxData();
    			}
    		}
    	};
    }





    void Execute() {
    	if (rxBufferCounter == 0 || registerTableSize == 0 || registerTable == nullptr) {
    		return;
    	}

    	if (Receive() == BusStatus::Success) {
    		Reply();
    	}
    	rxBufferCounter = 0;
    }



private:
    uint16 Crc16(uint8 *data, uint8 length) {
    	uint8 j;
    	uint16 crc = 0xFFFF;
    	while (length--) {
    		crc = crc ^ *data++;
    		for (j = 0; j < 8; j++) {
    			if (crc & 0x0001) {
    				crc = (crc >> 1) ^ 0xA001;
    			} else {
    				crc = crc >> 1;
    			}
    		}
    	}
    	return (crc << 8 | crc >> 8);
    }





    bool CheckCrc(uint8 *data, uint8 length) {
    	if (length < 2) {
    		return false;
    	}

    	uint16 calculated = Crc16(data, length - 2);
    	uint16 received = (data[length - 2] << 8) | data[length - 1];

    	return calculated == received;
    }





    uint8 BuildResponseBasis(uint8 function, uint8 *data) {
    	data[0] = slaveId;
    	data[1] = function;

    	return _MODBUS_RTU_PRESET_RSP_LENGTH;
    }





    void SendMessage(uint8 *data, uint8 dataSize) {
    	uint16 crc = Crc16(data, dataSize);

    	data[dataSize++] = crc >> 8;
    	data[dataSize++] = crc & 0x00FF;

    	dePin->Set();
    	serial->WriteArray<uint8>(data, dataSize);
    	dePin->Reset();
    }





    uint8 ResponseException(uint8 function, uint8 exceptionCode, uint8 *data) {
    	uint8 length = BuildResponseBasis(function + 0x80, data);
    	data[length++] = exceptionCode;

    	return length;
    }





    BusStatus Receive() {
    	uint8 function;

    	auto step = Step::std::function;
    	uint8 lengthToRead = _MODBUS_RTU_FUNCTION + 1;
    	uint8 receiveCounter = 0;


    	while (lengthToRead != 0) {

    		auto startTime = System::GetTick();
    		while (rxBufferCounter == receiveCounter) {
    			//if ((System::GetTick() - startTime) > rxTimeout) {
    			//	return BusStatus::ResponseTimedOut;
    			//}
    		}

    		receiveCounter++;
    		lengthToRead--;


    		if (lengthToRead == 0) {
    			if (rxBuffer[_MODBUS_RTU_SLAVE] != slaveId && rxBuffer[_MODBUS_RTU_SLAVE] != MODBUS_BROADCAST_ADDRESS) {
    				return BusStatus::InvalidSlaveID;
    			}

    			switch (step) {
    				case Step::std::function:
    					function = rxBuffer[_MODBUS_RTU_FUNCTION];
    					if (function == _FC_READ_HOLDING_REGISTERS) {
    						lengthToRead = 4;
    					} else if (function == _FC_WRITE_MULTIPLE_REGISTERS) {
    						lengthToRead = 5;
    					} else {
    						if (rxBuffer[_MODBUS_RTU_SLAVE] == slaveId || rxBuffer[_MODBUS_RTU_SLAVE] == MODBUS_BROADCAST_ADDRESS) {
    							uint8 responseLength = ResponseException(function, MODBUS_EXCEPTION_ILLEGAL_FUNCTION, txBuffer);
    							SendMessage(txBuffer, responseLength);
    							return BusStatus::InvalidFunction;
    						}

    						return BusStatus::UnknownError;
    					}
    					step = Step::Meta;
    				break;

    				case Step::Meta:
    					lengthToRead = _MODBUS_RTU_CHECKSUM_LENGTH;

    					if (function == _FC_WRITE_MULTIPLE_REGISTERS) {
    						lengthToRead += rxBuffer[_MODBUS_RTU_FUNCTION + 5];
    					}

    					if ((receiveCounter + lengthToRead) > _MODBUSINO_RTU_MAX_ADU_LENGTH) {
    						if (rxBuffer[_MODBUS_RTU_SLAVE] == slaveId || rxBuffer[_MODBUS_RTU_SLAVE] == MODBUS_BROADCAST_ADDRESS) {
    							uint8 responseLength = ResponseException(function, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, txBuffer);
    							SendMessage(txBuffer, responseLength);
    							return BusStatus::InvalidFunction;
    						}

    						return BusStatus::UnknownError;
    					}
    					step = Step::Data;
    				break;

    				default:
    				break;
    			}
    		}
    	}

    	if(!CheckCrc(rxBuffer, receiveCounter)) {
    		return BusStatus::InvalidCRC;
    	}

    	return BusStatus::Success;
    }





    void Reply() {
    	uint8 slave = rxBuffer[_MODBUS_RTU_SLAVE];
    	uint8 function = rxBuffer[_MODBUS_RTU_FUNCTION];
    	uint16 address = (rxBuffer[_MODBUS_RTU_FUNCTION + 1] << 8) + rxBuffer[_MODBUS_RTU_FUNCTION + 2];
    	uint16 nb = (rxBuffer[_MODBUS_RTU_FUNCTION + 3] << 8) + rxBuffer[_MODBUS_RTU_FUNCTION + 4];

        //uint8_t rsp[_MODBUSINO_RTU_MAX_ADU_LENGTH];
       // uint8_t rsp_length = 0;

    	uint8 sendDataSize = 0;

    	if(slave != slaveId) {
    		return;
    	}

    	if (address + nb > registerTableSize) {
    		sendDataSize = ResponseException(function, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, txBuffer);
    	} else {
    		if (function == _FC_READ_HOLDING_REGISTERS) {

    			sendDataSize = BuildResponseBasis(function, txBuffer);
    			txBuffer[sendDataSize++] = nb << 1;

    			for (uint16 i = address; i < address + nb; i++) {
    				txBuffer[sendDataSize++] = registerTable[i] >> 8;
    				txBuffer[sendDataSize++] = registerTable[i] & 0xFF;
    			}
    		} else {

    			for (uint16 i = address, j = 6; i < address + nb; i++, j += 2) {
    				// 6 and 7 - first value
    				uint16 newVal = (rxBuffer[_MODBUS_RTU_FUNCTION + j] << 8) + rxBuffer[_MODBUS_RTU_FUNCTION + j + 1];

    				if(onUpdateRegister != nullptr) {
    					if(onUpdateRegister(i, newVal)) {
    						registerTable[i] = newVal;
    					}
    				}
    			}

    			sendDataSize = BuildResponseBasis(function, txBuffer);

    			// 4 to copy the address (2) and the no. of registers
    			std::memcpy(txBuffer + sendDataSize, rxBuffer + sendDataSize, 4);
    			sendDataSize += 4;
    		}
    	}

    	SendMessage(txBuffer, sendDataSize);
    }

};



















