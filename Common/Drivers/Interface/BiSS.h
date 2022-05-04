#pragma once
#include <BSP.h>
#include <functional>



class BiSS {
public:
	struct SensorData {
		uint32 position = 0;
		uint8 status = 0;
		uint8 crc = 0;
		bool parseError = false;

		bool Error() {
			return !(status & 0b10);
		}

		bool Warning() {
			return !(status & 0b01);
		}

		bool CheckCrc() {
			uint64 crcData = position;
			crcData <<= 2;
			crcData |= status;
			crcData <<= 6;
			return BiSS::GetCrc((uint8*)&crcData, 9) == crc;
		}
	};


	enum class Step {
		FirstTwoBits,	// First two bits are always 1
		Ack,  			// Next 0 bits are the ACK bits. Number of ACK bits depends on encoder's latency and BiSS frequency.
		StartBit,		// Start bit is always 1
		Cds,			// CDS bit is always 0
		Position,		// 32 bits of POSITION = 0x00181907 = 1579271 counts
		Status,			// 2 STATUS bits = 0x03
		Crc				// 6 CRC bits = 0x3D
	};

private:
	SPI_TypeDef *spiTypeDef;


public:
	BiSS() {}

	BiSS(SPI_HandleTypeDef &spiHal):spiTypeDef(spiHal.Instance) { }
	BiSS(SPI_TypeDef *spi):spiTypeDef(spi) { }



	SensorData ReceiveData() {
		constexpr uint8 DATA_LEN = 15;
		uint8 data[DATA_LEN] = {0};

		LL_SPI_Enable(spiTypeDef);

		for(uint8 i = 0; i < DATA_LEN; i++) {
			while(!LL_SPI_IsActiveFlag_RXNE(spiTypeDef));
			data[i] = LL_SPI_ReceiveData8(spiTypeDef);
		}

		LL_SPI_Disable(spiTypeDef);

		//while(!LL_SPI_IsActiveFlag_RXNE(spiTypeDef));
		LL_SPI_ReceiveData8(spiTypeDef);

		return ParseRawData((uint8*)&data[0], DATA_LEN);
	}




	static inline bool IsBit(uint8 byte, uint8 bit) {
		return (byte & (1 << bit));
	}




	static SensorData ParseRawData(uint8* data, uint8 dataSize) {
		auto sensorData = SensorData();
		auto currentStep = Step::FirstTwoBits;
		uint8 firstTwoBitsCounter = 0;
		uint8 positionBitsCounter = 0;
		uint8 statusBitsCounter = 0;
		uint8 crcBitsCounter = 0;

		for(uint8 bit = 0; bit < dataSize * 8; bit++) {
			uint8 byte = static_cast<uint8>(bit / 8);
			uint8 bitInByte = 7 - (bit - (byte * 8));

			switch (currentStep) {
				case Step::FirstTwoBits: {
					if (IsBit(data[byte], bitInByte)) {
						firstTwoBitsCounter++;
					}

					if (firstTwoBitsCounter == 2) {
						currentStep = Step::Ack;
					}
				}
				break;

				case Step::Ack: {
					if (!IsBit(data[byte], bitInByte)) {
						currentStep = Step::StartBit;
					} else {
						sensorData.parseError = true;
						return sensorData;
					}
				}
				break;

				case Step::StartBit: {
					if (IsBit(data[byte], bitInByte)) {
						currentStep = Step::Cds;
					}
				}
				break;

				case Step::Cds: {
					if (!IsBit(data[byte], bitInByte)) {
						uint8_t allBits = dataSize * 8;
						static const uint8_t dataSize = 32 + 2 + 6; // POSITION + STATUS + CRC

						if(allBits - bit < dataSize) {
							sensorData.parseError = true;
							return sensorData;
						}

						currentStep = Step::Position;
					} else {
						sensorData.parseError = true;
						return sensorData;
					}
				}
				break;

				case Step::Position: {
					if (IsBit(data[byte], bitInByte)) {
						sensorData.position |= 1UL << (uint8_t)(31 - positionBitsCounter);
					}
					if (++positionBitsCounter == 32) {
						currentStep = Step::Status;
					}
				}
				break;

				case Step::Status: {
					if (IsBit(data[byte], bitInByte)) {
						sensorData.status |= 1UL << (uint8_t)(1 - statusBitsCounter);
					}
					if (++statusBitsCounter == 2) {
						currentStep = Step::Crc;
					}
				}
				break;

				case Step::Crc: {
					if (IsBit(data[byte], bitInByte)) {
						sensorData.crc |= 1UL << (uint8_t)(5 - crcBitsCounter);
					}
					if (++crcBitsCounter == 6) {
						return sensorData;
					}
				}
				break;
			};
		}

		return sensorData;
	}



	static uint8 GetCrc(uint8 *data, uint8 dataSize) {
		static const uint8 tableCrc6[64] = {
		  0x00, 0x03, 0x06, 0x05, 0x0C, 0x0F, 0x0A, 0x09,
		  0x18, 0x1B, 0x1E, 0x1D, 0x14, 0x17, 0x12, 0x11,
		  0x30, 0x33, 0x36, 0x35, 0x3C, 0x3F, 0x3A, 0x39,
		  0x28, 0x2B, 0x2E, 0x2D, 0x24, 0x27, 0x22, 0x21,
		  0x23, 0x20, 0x25, 0x26, 0x2F, 0x2C, 0x29, 0x2A,
		  0x3B, 0x38, 0x3D, 0x3E, 0x37, 0x34, 0x31, 0x32,
		  0x13, 0x10, 0x15, 0x16, 0x1F, 0x1C, 0x19, 0x1A,
		  0x0B, 0x08, 0x0D, 0x0E, 0x07, 0x04, 0x01, 0x02
		};


	  uint64 checkData = 0;
	  for(uint8 i = 0; i < dataSize; i++) {
	    checkData <<= 8;
	    checkData |= data[i];
	  }

	  uint64 tmp = (checkData >> 60) & 0x0000003F;
	  uint8 crc = ((checkData >> 54) & 0x0000003F);
	  tmp = crc ^ tableCrc6[tmp];
	  crc = ((checkData >> 48) & 0x0000003F);
	  tmp = crc ^ tableCrc6[tmp];
	  crc = ((checkData >> 42) & 0x0000003F);
	  tmp = crc ^ tableCrc6[tmp];
	  crc = ((checkData >> 36) & 0x0000003F);
	  tmp = crc ^ tableCrc6[tmp];
	  crc = ((checkData >> 30) & 0x0000003F);
	  tmp = crc ^ tableCrc6[tmp];
	  crc = ((checkData >> 24) & 0x0000003F);
	  tmp = crc ^ tableCrc6[tmp];
	  crc = ((checkData >> 18) & 0x0000003F);
	  tmp = crc ^ tableCrc6[tmp];
	  crc = ((checkData >> 12) & 0x0000003F);
	  tmp = crc ^ tableCrc6[tmp];
	  crc = ((checkData >> 6) & 0x0000003F);
	  tmp = crc ^ tableCrc6[tmp];
	  crc = (checkData & 0x0000003F);
	  tmp = crc ^ tableCrc6[tmp];
	  crc = tableCrc6[tmp];
	  return crc;
	}

};


