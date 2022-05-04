#pragma once
#include <BSP.h>
#include <Utilities/Data/ByteConverter.h>
#include <Utilities/Data/Bit.h>
#include <Utilities/Data/FIFO/StaticFifo.h>


class ModbusMaster {
public:
	enum class BusStatus : uint8 {
		Success = 0x00,				// success
		InvalidSlaveID = 0xE0,		// invalid response slave ID exception
		InvalidFunction = 0xE1,		// invalid response function exception
		ResponseTimedOut = 0xE2, 	// response timed out exception
		InvalidCRC = 0xE3, 			// invalid response CRC exception
	};


private:
	static constexpr size_t maxRxBuffer = 256;

	uint8 lastFunction;

	uint8 rxBuffer[maxRxBuffer] = { 0 };
	uint8 rxBufferCounter = 0;
	AUART *_serial;
	AGPIO *_dePin;

	uint8 _u8MBSlave;        									///< Modbus slave (1..255) initialized in begin()
	static const uint8 ku8MaxBufferSize = 64; 					///< size of response/transmit buffers
	uint16 _u16ReadAddress;             						///< slave register from which to read
	uint16 _u16ReadQty;                         				///< quantity of words to read
	uint16 _u16WriteAddress;            						///< slave register to which to write
	uint16 _u16WriteQty;                      	 				///< quantity of words to write
	uint16 _u16TransmitBuffer[ku8MaxBufferSize]; 				///< buffer containing data to transmit to Modbus slave; set via SetTransmitBuffer()
	uint16 *txBuffer; 							 				// from Wire.h -- need to clean this up Rx
	uint8 _u8TransmitBufferIndex;
	uint16 u16TransmitBufferLength;
	uint8 _u8ResponseBufferLength;

	// Modbus function codes for bit access
	static const uint8 ku8MBReadCoils = 0x01; 					///< Modbus function 0x01 Read Coils
	static const uint8 ku8MBReadDiscreteInputs = 0x02; 			///< Modbus function 0x02 Read Discrete Inputs
	static const uint8 ku8MBWriteSingleCoil = 0x05; 			///< Modbus function 0x05 Write Single Coil
	static const uint8 ku8MBWriteMultipleCoils = 0x0F; 			///< Modbus function 0x0F Write Multiple Coils

	// Modbus function codes for 16 bit access
	static const uint8 ku8MBReadHoldingRegisters = 0x03; 		///< Modbus function 0x03 Read Holding Registers
	static const uint8 ku8MBReadInputRegisters = 0x04; 			///< Modbus function 0x04 Read Input Registers
	static const uint8 ku8MBWriteSingleRegister = 0x06; 		///< Modbus function 0x06 Write Single Register
	static const uint8 ku8MBWriteMultipleRegisters = 0x10; 		///< Modbus function 0x10 Write Multiple Registers
	static const uint8 ku8MBMaskWriteRegister = 0x16; 			///< Modbus function 0x16 Mask Write Register
	static const uint8 ku8MBReadWriteMultipleRegisters = 0x17; 	///< Modbus function 0x17 Read Write Multiple Registers

	// Modbus timeout [milliseconds]
	static const uint16 ku16MBResponseTimeout = 100; 			///< Modbus timeout [milliseconds]

	// master function that conducts Modbus transactions
	BusStatus ModbusMasterTransaction(uint8 u8MBFunction);

	// preTransmission callback function; gets called before writing a Modbus message
	void (*_preTransmission)();

	// postTransmission callback function; gets called after a Modbus message has been sent
	void (*_postTransmission)();


public:
	// ---------- Modbus exception codes
	// Modbus protocol illegal function exception.
	static const uint8 ku8MBIllegalFunction = 0x01;

	// Modbus protocol illegal data address exception.
	static const uint8 ku8MBIllegalDataAddress = 0x02;

	// Modbus protocol illegal data value exception.
	static const uint8 ku8MBIllegalDataValue = 0x03;

	// Modbus protocol slave device failure exception.
	static const uint8 ku8MBSlaveDeviceFailure = 0x04;


	// --------- Class-defined success/exception codes
	// ModbusMaster success.
	static const uint8 ku8MBSuccess = 0x00;

	// ModbusMaster invalid response slave ID exception.
	static const uint8 ku8MBInvalidSlaveID = 0xE0;

	// ModbusMaster invalid response function exception.
	static const uint8 ku8MBInvalidFunction = 0xE1;

	// ModbusMaster response timed out exception.
	static const uint8 ku8MBResponseTimedOut = 0xE2;

	// ModbusMaster invalid response CRC exception.
	static const uint8 ku8MBInvalidCRC = 0xE3;



public:
	ModbusMaster() {
		_preTransmission = 0;
		_postTransmission = 0;
	}




	void begin(uint8 slave, AUART &serial, AUART::Parameters parameters, AGPIO &dePin) {
		_serial = &serial;
		_dePin = &dePin;

		_dePin->Reset();

		_serial->SetParameters(parameters);

		_serial->SetContinuousAsyncRxMode(true);

		_serial->onInterrupt = [this](auto type) {
			if(type == AUART::Irq::Rx) {
				if(rxBufferCounter < maxRxBuffer) {
					rxBuffer[rxBufferCounter++] = _serial->GetLastRxData();
				}
			}
		};


		_u8MBSlave = slave;
		_u8TransmitBufferIndex = 0;
		u16TransmitBufferLength = 0;
		rxBufferCounter = 0;
	}





	void beginTransmission(uint16 u16Address) {
		_u16WriteAddress = u16Address;
		_u8TransmitBufferIndex = 0;
		u16TransmitBufferLength = 0;
	}






	void sendBit(bool data) {
		uint8 txBitIndex = u16TransmitBufferLength % 16;
		if ((u16TransmitBufferLength >> 4) < ku8MaxBufferSize) {
			if (0 == txBitIndex) {
				_u16TransmitBuffer[_u8TransmitBufferIndex] = 0;
			}
			Bit::Write(_u16TransmitBuffer[_u8TransmitBufferIndex], txBitIndex, data);
			u16TransmitBufferLength++;
			_u8TransmitBufferIndex = u16TransmitBufferLength >> 4;
		}
	}





	void send(uint16 data) {
		if (_u8TransmitBufferIndex < ku8MaxBufferSize) {
			_u16TransmitBuffer[_u8TransmitBufferIndex++] = data;
			u16TransmitBufferLength = _u8TransmitBufferIndex << 4;
		}
	}





	void send(uint32 data) {
		send(ByteConverter::GetLow(data));
		send(ByteConverter::GetHigh(data));
	}





	void send(uint8 data) {
		send((uint16)data);
	}






	void preTransmission(void (*preTransmission)()) {
		_preTransmission = preTransmission;
	}





	void postTransmission(void (*postTransmission)()) {
		_postTransmission = postTransmission;
	}





	BusStatus getAnswer(uint16 *data) {
		int16 bytesLeft = 8;
		uint8 i = 0;
		BusStatus u8MBStatus = BusStatus::Success;


		// loop until we run out of time or bytes, or an error occurs
		auto startTime = System::GetTick();
		while (rxBufferCounter < bytesLeft && u8MBStatus == BusStatus::Success) {

			// evaluate slave ID, function code once enough bytes have been read
			if (rxBufferCounter >= 5) {
				// verify response is for correct Modbus slave
				if (rxBuffer[0] != _u8MBSlave) {
					u8MBStatus = BusStatus::InvalidSlaveID;
					break;
				}

				// verify response is for correct Modbus function code (mask exception bit 7)
				if ((rxBuffer[1] & 0x7F) != lastFunction) {
					u8MBStatus = BusStatus::InvalidFunction;
					break;
				}

				// check whether Modbus exception occurred; return Modbus Exception Code
				if (Bit::Check(rxBuffer[1], 7U)) {
					u8MBStatus = static_cast<BusStatus>(rxBuffer[2]);
					break;
				}

				// evaluate returned Modbus function code
				switch (rxBuffer[1]) {
					case ku8MBReadCoils:
					case ku8MBReadDiscreteInputs:
					case ku8MBReadInputRegisters:
					case ku8MBReadHoldingRegisters:
					case ku8MBReadWriteMultipleRegisters:
						bytesLeft = 5 + rxBuffer[2];
					break;

					case ku8MBWriteSingleCoil:
					case ku8MBWriteMultipleCoils:
					case ku8MBWriteSingleRegister:
					case ku8MBWriteMultipleRegisters:
						bytesLeft = 8;
					break;

					case ku8MBMaskWriteRegister:
						bytesLeft = 10;
					break;
				}
			}

			if ((System::GetTick() - startTime) > ku16MBResponseTimeout) {
				u8MBStatus = BusStatus::ResponseTimedOut;
			}
		}

		// verify response is large enough to inspect further
		if (u8MBStatus == BusStatus::Success && rxBufferCounter >= 5) {
			// calculate CRC
			uint16 u16CRC = 0xFFFF;
			for (uint8 i = 0; i < (rxBufferCounter - 2); i++) {
				u16CRC = Crc16Update(u16CRC, rxBuffer[i]);
			}

			// verify CRC
			if (
				u8MBStatus == BusStatus::Success &&
				(ByteConverter::GetLow(u16CRC) != rxBuffer[rxBufferCounter - 2] ||
				ByteConverter::GetHigh(u16CRC) != rxBuffer[rxBufferCounter - 1])
			) {
				u8MBStatus = BusStatus::InvalidCRC;
			}
		}


		// disassemble ADU into words
		if (u8MBStatus == BusStatus::Success) {
			// evaluate returned Modbus function code
			switch (rxBuffer[1]) {
				case ku8MBReadCoils:
				case ku8MBReadDiscreteInputs:
					// load bytes into word; response bytes are ordered L, H, L, H, ...
					for (i = 0; i < (rxBuffer[2] >> 1); i++) {
						if (i < ku8MaxBufferSize) {
							data[i] = ByteConverter::Create(rxBuffer[2 * i + 4], rxBuffer[2 * i + 3]);
						}

						_u8ResponseBufferLength = i;
					}

					// in the event of an odd number of bytes, load last byte into zero-padded word
					if (rxBuffer[2] % 2) {
						if (i < ku8MaxBufferSize) {
							data[i] = ByteConverter::Create(0, rxBuffer[2 * i + 3]);
						}

						_u8ResponseBufferLength = i + 1;
					}
				break;


				case ku8MBReadInputRegisters:
				case ku8MBReadHoldingRegisters:
				case ku8MBReadWriteMultipleRegisters:
					// load bytes into word; response bytes are ordered H, L, H, L, ...
					for (uint8 i = 0; i < (rxBuffer[2] >> 1); i++) {
						if (i < ku8MaxBufferSize) {
							data[i] = ByteConverter::Create(rxBuffer[2 * i + 3], rxBuffer[2 * i + 4]);
						}

						_u8ResponseBufferLength = i;
					}
				break;
			}
		}

		rxBufferCounter = 0;

		return u8MBStatus;
	}






	uint8 setTransmitBuffer(uint8 u8Index, uint16 u16Value) {
		if (u8Index < ku8MaxBufferSize) {
			_u16TransmitBuffer[u8Index] = u16Value;
			return ku8MBSuccess;
		} else {
			return ku8MBIllegalDataAddress;
		}
	}





	void clearTransmitBuffer() {
		uint8 i;

		for (i = 0; i < ku8MaxBufferSize; i++) {
			_u16TransmitBuffer[i] = 0;
		}
	}





	BusStatus readCoils(uint16 u16ReadAddress, uint16 u16BitQty) {
		_u16ReadAddress = u16ReadAddress;
		_u16ReadQty = u16BitQty;
		return ModbusMasterTransaction(ku8MBReadCoils);
	}





	BusStatus readDiscreteInputs(uint16 u16ReadAddress, uint16 u16BitQty) {
		_u16ReadAddress = u16ReadAddress;
		_u16ReadQty = u16BitQty;
		return ModbusMasterTransaction(ku8MBReadDiscreteInputs);
	}





	BusStatus readHoldingRegisters(uint16 u16ReadAddress, uint16 u16ReadQty) {
		_u16ReadAddress = u16ReadAddress;
		_u16ReadQty = u16ReadQty;
		return ModbusMasterTransaction(ku8MBReadHoldingRegisters);
	}





	BusStatus readInputRegisters(uint16 u16ReadAddress, uint8 u16ReadQty) {
		_u16ReadAddress = u16ReadAddress;
		_u16ReadQty = u16ReadQty;
		return ModbusMasterTransaction(ku8MBReadInputRegisters);
	}





	BusStatus writeSingleCoil(uint16 u16WriteAddress, uint8 u8State) {
		_u16WriteAddress = u16WriteAddress;
		_u16WriteQty = (u8State ? 0xFF00 : 0x0000);
		return ModbusMasterTransaction(ku8MBWriteSingleCoil);
	}





	BusStatus writeSingleRegister(uint16 u16WriteAddress, uint16 u16WriteValue) {
		_u16WriteAddress = u16WriteAddress;
		_u16WriteQty = 0;
		_u16TransmitBuffer[0] = u16WriteValue;
		return ModbusMasterTransaction(ku8MBWriteSingleRegister);
	}





	BusStatus writeMultipleCoils(uint16 u16WriteAddress, uint16 u16BitQty) {
		_u16WriteAddress = u16WriteAddress;
		_u16WriteQty = u16BitQty;
		return ModbusMasterTransaction(ku8MBWriteMultipleCoils);
	}





	BusStatus writeMultipleCoils() {
		_u16WriteQty = u16TransmitBufferLength;
		return ModbusMasterTransaction(ku8MBWriteMultipleCoils);
	}





	BusStatus writeMultipleRegisters(uint16 u16WriteAddress, uint16 u16WriteQty) {
		_u16WriteAddress = u16WriteAddress;
		_u16WriteQty = u16WriteQty;
		return ModbusMasterTransaction(ku8MBWriteMultipleRegisters);
	}





	BusStatus writeMultipleRegisters() {
		_u16WriteQty = _u8TransmitBufferIndex;
		return ModbusMasterTransaction(ku8MBWriteMultipleRegisters);
	}





	BusStatus maskWriteRegister(uint16 u16WriteAddress, uint16 u16AndMask, uint16 u16OrMask) {
		_u16WriteAddress = u16WriteAddress;
		_u16TransmitBuffer[0] = u16AndMask;
		_u16TransmitBuffer[1] = u16OrMask;
		return ModbusMasterTransaction(ku8MBMaskWriteRegister);
	}





	BusStatus readWriteMultipleRegisters(uint16 u16ReadAddress, uint16 u16ReadQty, uint16 u16WriteAddress, uint16 u16WriteQty) {
		_u16ReadAddress = u16ReadAddress;
		_u16ReadQty = u16ReadQty;
		_u16WriteAddress = u16WriteAddress;
		_u16WriteQty = u16WriteQty;
		return ModbusMasterTransaction(ku8MBReadWriteMultipleRegisters);
	}





	BusStatus readWriteMultipleRegisters(uint16 u16ReadAddress, uint16 u16ReadQty) {
		_u16ReadAddress = u16ReadAddress;
		_u16ReadQty = u16ReadQty;
		_u16WriteQty = _u8TransmitBufferIndex;
		return ModbusMasterTransaction(ku8MBReadWriteMultipleRegisters);
	}





	BusStatus ModbusMasterTransaction(uint8 function) {
		lastFunction = function;
		uint8 u8ModbusADU[256];
		uint8 u8ModbusADUSize = 0;
		uint8 u8Qty;

		// assemble Modbus Request Application Data Unit
		u8ModbusADU[u8ModbusADUSize++] = _u8MBSlave;
		u8ModbusADU[u8ModbusADUSize++] = lastFunction;

		switch (lastFunction) {
			case ku8MBReadCoils:
			case ku8MBReadDiscreteInputs:
			case ku8MBReadInputRegisters:
			case ku8MBReadHoldingRegisters:
			case ku8MBReadWriteMultipleRegisters:
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetHigh(_u16ReadAddress);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(_u16ReadAddress);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetHigh(_u16ReadQty);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(_u16ReadQty);
			break;
		}



		switch (lastFunction) {
			case ku8MBWriteSingleCoil:
			case ku8MBMaskWriteRegister:
			case ku8MBWriteMultipleCoils:
			case ku8MBWriteSingleRegister:
			case ku8MBWriteMultipleRegisters:
			case ku8MBReadWriteMultipleRegisters:
				u8ModbusADU[u8ModbusADUSize++] =  ByteConverter::GetHigh(_u16WriteAddress);
				u8ModbusADU[u8ModbusADUSize++] =  ByteConverter::GetLow(_u16WriteAddress);
			break;
		}



		switch (lastFunction) {
			case ku8MBWriteSingleCoil:
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetHigh(_u16WriteQty);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(_u16WriteQty);
			break;

			case ku8MBWriteSingleRegister:
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetHigh(_u16TransmitBuffer[0]);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(_u16TransmitBuffer[0]);
			break;

			case ku8MBWriteMultipleCoils:
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetHigh(_u16WriteQty);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(_u16WriteQty);
				u8Qty = (_u16WriteQty % 8) ? ((_u16WriteQty >> 3) + 1) : (_u16WriteQty >> 3);
				u8ModbusADU[u8ModbusADUSize++] = u8Qty;
				for (uint8 i = 0; i < u8Qty; i++) {
					switch (i % 2) {
						case 0: // i is even
							u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(_u16TransmitBuffer[i >> 1]);
						break;

						case 1: // i is odd
							u8ModbusADU[u8ModbusADUSize++] =  ByteConverter::GetHigh(_u16TransmitBuffer[i >> 1]);
						break;
					}
				}
			break;

			case ku8MBWriteMultipleRegisters:
			case ku8MBReadWriteMultipleRegisters:
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetHigh(_u16WriteQty);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(_u16WriteQty);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow((uint16)(_u16WriteQty << 1));

				for (uint8 i = 0; i <  ByteConverter::GetLow(_u16WriteQty); i++) {
					u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetHigh(_u16TransmitBuffer[i]);
					u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(_u16TransmitBuffer[i]);
				}
			break;

			case ku8MBMaskWriteRegister:
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetHigh(_u16TransmitBuffer[0]);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(_u16TransmitBuffer[0]);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetHigh(_u16TransmitBuffer[1]);
				u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(_u16TransmitBuffer[1]);
			break;
		}


		// append CRC
		uint16 u16CRC = 0xFFFF;
		for (uint8 i = 0; i < u8ModbusADUSize; i++) {
			u16CRC = Crc16Update(u16CRC, u8ModbusADU[i]);
		}
		u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetLow(u16CRC);
		u8ModbusADU[u8ModbusADUSize++] = ByteConverter::GetHigh(u16CRC);
		u8ModbusADU[u8ModbusADUSize] = 0;


		// transmit request
		if (_preTransmission) {
			_preTransmission();
		}


		_dePin->Set();
		_serial->WriteArray<uint8>(&u8ModbusADU[0], u8ModbusADUSize);
		_dePin->Reset();


		u8ModbusADUSize = 0;
		if (_postTransmission) {
			_postTransmission();
		}



		_u8TransmitBufferIndex = 0;
		u16TransmitBufferLength = 0;
		return BusStatus::Success;
	}



private:
	uint16 Crc16Update(uint16 crc, uint8 a) {
		crc ^= a;
		for (int i = 0; i < 8; ++i) {
			crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
		}
		return crc;
	}


};


