#pragma once
#include <System/System.h>
#include <System/SystemUtilities.h>
#include "../../I2CAdapter.h"

using namespace SystemUtilities;


using AI2C = class I2CAdapterF4;



class I2CAdapterF4 : public I2CAdapter {
public:
	I2CAdapterF4() { }
	I2CAdapterF4(I2C_TypeDef *i2c, uint32 busClockHz):I2CAdapter(i2c, busClockHz) { }



	virtual void IrqEventHandler() override {
		if(parameters.mode ==  Mode::Master) {
			MasterIrqEvent();
		} else {
			SlaveIrqEvent();
		}
	}



	virtual void IrqErrorHandler() override {

	}



public:
	virtual Status::statusType StartSlaveListen() {
		return Status::notSupported;
	}


	virtual Status::statusType StopSlaveListen() {
		return Status::notSupported;
	}





	virtual Status::statusType CheckDevice(uint8 deviceAddress, uint16 repeat) override {
		if(!WaitBUSY(false)) {
			return Status::busy;
		}

		if (!LL_I2C_IsEnabled(i2cHandle)) {
			LL_I2C_Enable(i2cHandle);
		}

		LL_I2C_DisableBitPOS(i2cHandle);

		do {
			LL_I2C_GenerateStartCondition(i2cHandle);

			if(!WaitSB(false)) {
				return Status::timeout;
			}

			LL_I2C_TransmitData8(i2cHandle, (deviceAddress << 1) & ~0x01);


			if(!WaitCondition([this]() {
				return LL_I2C_IsActiveFlag_ADDR(i2cHandle) && LL_I2C_IsActiveFlag_AF(i2cHandle);
			}, timeout)) {
				break;
			}

			if (LL_I2C_IsActiveFlag_ADDR(i2cHandle)) {
				LL_I2C_GenerateStopCondition(i2cHandle);
				LL_I2C_ClearFlag_ADDR(i2cHandle);

				if(!WaitBUSY(false)) {
					return Status::timeout;
				}

				return Status::ok;
			} else {
				LL_I2C_GenerateStopCondition(i2cHandle);
				LL_I2C_ClearFlag_AF(i2cHandle);

				if(!WaitBUSY(false)) {
					return Status::timeout;
				}
			}

			repeat--;
		} while (repeat != 0);

		return Status::error;
	}





	virtual Status::statusType CheckDeviceAsync(uint8 deviceAddress, uint16 repeat) override {
		return Status::notSupported;
	}





	virtual uint8 Scan(uint8 *listBuffer, uint8 size) override {
		uint8 count = 0;
		for (uint8 i = 0; i < 127 && i <= size; i++) {
			if (CheckDevice(i, 1) == Status::ok) {
				*listBuffer = i;
				listBuffer++;
				count++;
			}
		}
		return count;
	}





	virtual uint8 ScanAsync(uint8 *listBuffer, uint8 size) override {
		return Status::notSupported;
	}



private:
	inline void SlaveIrqEvent() {

	}





	inline void MasterIrqEvent() {

	}



protected:
	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		LL_I2C_InitTypeDef init = {
			.PeripheralMode = LL_I2C_MODE_I2C,
			.ClockSpeed = 100000,
			.DutyCycle = LL_I2C_DUTYCYCLE_2,
			.OwnAddress1 = 0,
			.TypeAcknowledge = LL_I2C_ACK,
			.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT
		};

		LL_I2C_Init(i2cHandle, &init);

		LL_I2C_SetOwnAddress2(i2cHandle, 0);
		LL_I2C_DisableOwnAddress2(i2cHandle);
		LL_I2C_DisableGeneralCall(i2cHandle);

		if(parameters.clockStretching) {
			LL_I2C_EnableClockStretching(i2cHandle);
		} else {
			LL_I2C_DisableClockStretching(i2cHandle);
		}

		return AfterInitialization();
	}




public:
	virtual Status::statusType WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *writeData, uint32 dataSize) override {
		return Status::notSupported;
	}





	virtual Status::statusType ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		state = Status::busy;

		rxDataNeed = dataSize;
		rxDataCounter = 0;
		rxDataPointer = readData;


		if(!WaitBUSY(false)) {
			state = Status::ready;
			return Status::busy;
		}

		if (!LL_I2C_IsEnabled(i2cHandle)) {
			LL_I2C_Enable(i2cHandle);
		}

		LL_I2C_DisableBitPOS(i2cHandle);

		auto status = RequestMemoryRead(device, address, addressSize);
		if (status != Status::ok) {
			state = Status::ready;
			return status;
		}


		switch (rxDataNeed) {
			case 0: {
				LL_I2C_GenerateStopCondition(i2cHandle);
				LL_I2C_ClearFlag_ADDR(i2cHandle);
			}
			break;

			case 1: {
				//_I2C_Port->CR1 &= ~I2C_CR1_ACK; // Сбрасываем ACK
				LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK);
				LL_I2C_GenerateStopCondition(i2cHandle);
				LL_I2C_ClearFlag_ADDR(i2cHandle);
			}
			break;

			case 2: {
				//_I2C_Port->CR1 &= ~I2C_CR1_ACK; // Сбрасываем ACK
				LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK);
				LL_I2C_EnableBitPOS(i2cHandle);
				LL_I2C_ClearFlag_ADDR(i2cHandle);
			}
			break;

			default: {
				LL_I2C_ClearFlag_ADDR(i2cHandle);
			}
			break;
		}


		while (rxDataCounter < rxDataNeed) {
			switch (rxDataNeed - rxDataCounter) {
				case 1: {
					if (!WaitRXNE()) {
						state = Status::ready;
						return Status::timeout;
					}
					ReceiveData();
				}
				break;


				case 2: {
					if (!WaitBTF(false)) {
						state = Status::ready;
						return Status::timeout;
					}

					LL_I2C_GenerateStopCondition(i2cHandle);
					ReceiveData(2);
				}
				break;


				case 3: {
					if (!WaitBTF(false)) {
						state = Status::ready;
						return Status::timeout;
					}

					LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK); // _I2C_Port->CR1 &= ~I2C_CR1_ACK;
					ReceiveData();

					if (!WaitBTF(false)) {
						state = Status::ready;
						return Status::timeout;
					}

					LL_I2C_GenerateStopCondition(i2cHandle);
					ReceiveData(2);
				}
				break;


				default: {
					if (!WaitRXNE()) {
						state = Status::ready;
						return Status::timeout;
					}

					ReceiveData();
					if (LL_I2C_IsActiveFlag_BTF(i2cHandle)) {
						ReceiveData();
					}
				}
				break;
			}
		}

		state = Status::ready;
		return Status::ok;
	}





	virtual Status::statusType WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return Status::notSupported;
	}

	virtual Status::statusType ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return Status::notSupported;
	}



private:

	Status::statusType RequestMemoryRead(uint8 device, uint16 address, uint8 addressSize) {
		return Status::error;
	}





	Status::statusType RequestMemoryWrite(uint8 device, uint16 address, uint8 addressSize) {
		return Status::error;
	}





	bool WaitRXNE() {
		static auto rxne = [this]() { return LL_I2C_IsActiveFlag_RXNE(i2cHandle); };

		static auto stop = [this]() {
			if (LL_I2C_IsActiveFlag_STOP(i2cHandle)) {
				LL_I2C_ClearFlag_STOP(i2cHandle); //_I2C_Port->SR1 &= ~I2C_SR1_STOPF;
				return false;
			}
			return true;
		};

		return WaitConditionDouble(rxne, stop, timeout);
	}



	bool WaitBTF(bool waitSet) {
		static auto btf = [this, waitSet]() {
			return static_cast<bool>(LL_I2C_IsActiveFlag_BTF(i2cHandle)) == waitSet;
		};

		return WaitCondition(btf, timeout);
	}



	bool WaitBUSY(bool waitSet) {
		static auto btf = [this, waitSet]() {
			return static_cast<bool>(LL_I2C_IsActiveFlag_BUSY(i2cHandle)) == waitSet;
		};

		return WaitCondition(btf, timeout);
	}



	bool WaitSB(bool waitSet) {
		static auto btf = [this, waitSet]() {
			return static_cast<bool>(LL_I2C_IsActiveFlag_SB(i2cHandle)) == waitSet;
		};

		return WaitCondition(btf, timeout);
	}




	inline void ReceiveData(uint8 count = 1) {
		while(count-- > 0) {
			*rxDataPointer++ = LL_I2C_ReceiveData8(i2cHandle);
			rxDataCounter++;
		}
	}

};















