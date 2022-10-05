#pragma once
#include <System/System.h>
#include "../../I2CAdapter.h"




using AI2C = class I2CAdapterG0;


class I2CAdapterG0 : public I2CAdapter {
public:
	I2CAdapterG0() { }
	I2CAdapterG0(I2C_TypeDef *i2c, uint32 busClockHz):I2CAdapter(i2c, busClockHz) { }



	virtual void IrqEventHandler() override {
		if(parameters.mode ==  Mode::Master) {
			MasterIrqEvent();
		} else {
			SlaveIrqEvent();
		}
	}



	virtual void IrqErrorHandler() override {
		if(!LL_I2C_IsEnabledIT_ERR(i2cHandle)) {
			return;
		}


		if (state == Status::listen) {
			// Disable all interrupts, except interrupts related to LISTEN state
			LL_I2C_DisableIT_TX(i2cHandle);
			LL_I2C_DisableIT_RX(i2cHandle);
		} else {
			// Disable all interrupts
			LL_I2C_DisableIT_ADDR(i2cHandle);
			LL_I2C_DisableIT_NACK(i2cHandle);
			LL_I2C_DisableIT_ERR(i2cHandle);
			LL_I2C_DisableIT_STOP(i2cHandle);
			LL_I2C_DisableIT_TX(i2cHandle);
			LL_I2C_DisableIT_RX(i2cHandle);
			state = Status::ready;
		}


		if (LL_I2C_IsActiveFlag_BERR(i2cHandle)) {
			CallError(Error::MisplacedStartStop);
			LL_I2C_ClearFlag_BERR(i2cHandle);
		}

		if (LL_I2C_IsActiveFlag_OVR(i2cHandle)) {
			CallError(Error::OverUnderRun);
			LL_I2C_ClearFlag_OVR(i2cHandle);
		}

		if (LL_I2C_IsActiveFlag_ARLO(i2cHandle)) {
			CallError(Error::ArbitrationLost);
			LL_I2C_ClearFlag_ARLO(i2cHandle);
		}
	}



	inline void IrqHandler() {
		//LL_I2C_IsActiveFlag_BERR(i2cHandle) || LL_I2C_IsActiveFlag_ARLO(i2cHandle) || LL_I2C_IsActiveFlag_OVR(i2cHandle)
		if (i2cHandle->ISR & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
			IrqErrorHandler();
		} else {
			IrqEventHandler();
		}
	}


public:
	virtual Status::statusType StartSlaveListen() {
		LL_I2C_SetOwnAddress1(i2cHandle, parameters.slaveAddress, LL_I2C_OWNADDRESS1_7BIT); // TODO: Добавить LL_I2C_OWNADDRESS1_7BIT
		LL_I2C_EnableOwnAddress1(i2cHandle);
		LL_I2C_EnableIT_ADDR(i2cHandle);
		LL_I2C_EnableIT_NACK(i2cHandle);
		LL_I2C_EnableIT_ERR(i2cHandle);
		LL_I2C_EnableIT_STOP(i2cHandle);
		return Status::ok;
	}


	virtual Status::statusType StopSlaveListen() {
		LL_I2C_DisableOwnAddress1(i2cHandle);
		LL_I2C_DisableIT_ADDR(i2cHandle);
		LL_I2C_DisableIT_NACK(i2cHandle);
		LL_I2C_DisableIT_ERR(i2cHandle);
		LL_I2C_DisableIT_STOP(i2cHandle);
		LL_I2C_DisableIT_TX(i2cHandle);
		LL_I2C_DisableIT_RX(i2cHandle);
		return Status::ok;
	}


	virtual Status::statusType CheckDevice(uint8 deviceAddress, uint16 repeat) override {
		return Status::notSupported;
	}


	virtual Status::statusType CheckDeviceAsync(uint8 deviceAddress, uint16 repeat) override {
		return Status::notSupported;
	}


	virtual Status::info<uint8> Scan(uint8 *listBuffer, uint8 size) override {
		return { Status::notSupported };
	}


	virtual Status::info<uint8> ScanAsync(uint8 *listBuffer, uint8 size) override {
		return { Status::notSupported };
	}



private:
	inline void SlaveIrqEvent() {

		// AddrCallback
		if (LL_I2C_IsActiveFlag_ADDR(i2cHandle)) {
			if (LL_I2C_GetAddressMatchCode(i2cHandle) != parameters.slaveAddress) {
				CallError(Error::SlaveAddressMatch);
				LL_I2C_ClearFlag_ADDR(i2cHandle);
				return;
			}

			if (LL_I2C_GetTransferDirection(i2cHandle) == LL_I2C_DIRECTION_WRITE) {
				LL_I2C_EnableIT_RX(i2cHandle);
			} else {
				LL_I2C_EnableIT_TX(i2cHandle);
			}

			LL_I2C_ClearFlag_ADDR(i2cHandle);
			return;
		}

		if (LL_I2C_IsActiveFlag_TXIS(i2cHandle)) {
			auto data = CallSlaveWrite();
			LL_I2C_TransmitData8(i2cHandle, data);
			return;
		}


		if (LL_I2C_IsActiveFlag_RXNE(i2cHandle)) {
			auto data = LL_I2C_ReceiveData8(i2cHandle);
			CallSlaveRead(data);
			return;
		}


		if (LL_I2C_IsActiveFlag_NACK(i2cHandle)) {
			LL_I2C_ClearFlag_NACK(i2cHandle);
			return;
		}


		if (LL_I2C_IsActiveFlag_STOP(i2cHandle)) {
			CallSlaveEndTransfer();

			if (!LL_I2C_IsActiveFlag_TXE(i2cHandle)) {
				LL_I2C_ClearFlag_TXE(i2cHandle);
			}

			LL_I2C_ClearFlag_STOP(i2cHandle);
			return;
		}


		if (!LL_I2C_IsActiveFlag_TXE(i2cHandle)) {
			// Do nothing
			// This Flag will be set by hardware when the TXDR register is empty
			// If needed, use LL_I2C_ClearFlag_TXE() interface to flush the TXDR register
			return;
		}


		CallError(Error::None); // TODO: Error set
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
			.Timing = 0x10B00000, // TODO: Добавить расчет с I2C_Timing_Configuration_V1.0.1
			.AnalogFilter = LL_I2C_ANALOGFILTER_ENABLE, // TODO: Добавить
			.DigitalFilter = parameters.digitalFilter, // TODO: Добавить проверку
			.OwnAddress1 = parameters.slaveAddress,
			.TypeAcknowledge = LL_I2C_ACK,
			.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT // TODO: Добавить
		};

		LL_I2C_Init(i2cHandle, &init);

		LL_I2C_EnableAutoEndMode(i2cHandle);

		LL_I2C_SetOwnAddress2(i2cHandle, 0, LL_I2C_OWNADDRESS2_NOMASK);
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
	virtual Status::statusType WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return Status::notSupported;
	}

	virtual Status::statusType ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return Status::notSupported;
	}

	virtual Status::statusType WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return Status::notSupported;
	}

	virtual Status::statusType ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return Status::notSupported;
	}

};















