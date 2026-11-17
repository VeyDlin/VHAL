#pragma once
#include <Adapter/I2CAdapter.h>
#include "stm32g4xx_ll_i2c.h"


using AI2C = class I2CAdapterG4;



// I2C v2 IP (same as G0), STM32G4 port, slave mode only.
//
// Slave behaviour:
//   - 7-bit own address (Parameters::slaveAddress), clock stretching enabled by default.
//   - onSlaveAddressMatch(isRead) on every address match, before the first data byte.
//   - onSlaveRead(data) for every received byte (write transactions).
//   - onSlaveWrite() for every byte the master reads (TX), called once per byte.
//   - onSlaveStop(wasRead) and onSlaveEndTransfer() on every STOP condition.
//   - onError(...) on bus error, arbitration loss and overrun. The listen state stays armed,
//     so the next transaction is served normally. A STOP callback may not follow the aborted transfer.
//
// Timing: values are for the I2C kernel clock = HSI16 (16 MHz) with PRESC = 0 (62.5 ns tick).
//   Fast (400 kHz):     SCLL 25, SCLH 13, SCLDEL 3, SDADEL 2 -> TIMINGR 0x00320D19
//   Standard (100 kHz): SCLL 79, SCLH 79, SCLDEL 3, SDADEL 2 -> TIMINGR 0x00324F4F
// Other speeds or clock frequencies return ResultStatus::notSupported from SetParameters.
class I2CAdapterG4 : public I2CAdapter<I2C_TypeDef> {
private:
	static constexpr uint32 HsiClockHz = 16000000;
	static constexpr uint32 TimingStandard16MHz = 0x00324F4F;
	static constexpr uint32 TimingFast16MHz = 0x00320D19;

	bool slaveTransferIsRead = false;



public:
	I2CAdapterG4() { }
	I2CAdapterG4(I2C_TypeDef *i2c, uint32 busClockHz):I2CAdapter(i2c, busClockHz) { }



	virtual void IrqEventHandler() override {
		if(parameters.mode == Mode::Slave) {
			SlaveIrqEvent();
		}
	}



	virtual void IrqErrorHandler() override {
		if(!LL_I2C_IsEnabledIT_ERR(i2cHandle)) {
			return;
		}

		if(LL_I2C_IsActiveFlag_BERR(i2cHandle)) {
			LL_I2C_ClearFlag_BERR(i2cHandle);
			SlaveAbortTransfer();
			CallError(Error::MisplacedStartStop);
		}

		if(LL_I2C_IsActiveFlag_ARLO(i2cHandle)) {
			LL_I2C_ClearFlag_ARLO(i2cHandle);
			SlaveAbortTransfer();
			CallError(Error::ArbitrationLost);
		}

		if(LL_I2C_IsActiveFlag_OVR(i2cHandle)) {
			LL_I2C_ClearFlag_OVR(i2cHandle);
			SlaveAbortTransfer();
			CallError(Error::OverUnderRun);
		}
	}



	// Single entry point for both I2C1_EV and I2C1_ER interrupt vectors.
	inline void IrqHandler() {
		if(i2cHandle->ISR & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
			IrqErrorHandler();
		} else {
			IrqEventHandler();
		}
	}



	virtual ResultStatus StartSlaveListen() override {
		LL_I2C_EnableOwnAddress1(i2cHandle);
		LL_I2C_EnableIT_ADDR(i2cHandle);
		LL_I2C_EnableIT_NACK(i2cHandle);
		LL_I2C_EnableIT_ERR(i2cHandle);
		LL_I2C_EnableIT_STOP(i2cHandle);
		return ResultStatus::ok;
	}



	virtual ResultStatus StopSlaveListen() override {
		LL_I2C_DisableOwnAddress1(i2cHandle);
		LL_I2C_DisableIT_ADDR(i2cHandle);
		LL_I2C_DisableIT_NACK(i2cHandle);
		LL_I2C_DisableIT_ERR(i2cHandle);
		LL_I2C_DisableIT_STOP(i2cHandle);
		LL_I2C_DisableIT_TX(i2cHandle);
		LL_I2C_DisableIT_RX(i2cHandle);
		return ResultStatus::ok;
	}



	virtual ResultStatus CheckDevice(uint8 deviceAddress, uint16 repeat) override {
		return ResultStatus::notSupported;
	}



	virtual ResultStatus CheckDeviceAsync(uint8 deviceAddress, uint16 repeat) override {
		return ResultStatus::notSupported;
	}



	virtual Result<uint8> Scan(uint8 *listBuffer, uint8 size) override {
		return { ResultStatus::notSupported };
	}



	virtual Result<uint8> ScanAsync(uint8 *listBuffer, uint8 size) override {
		return { ResultStatus::notSupported };
	}



	virtual ResultStatus WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return ResultStatus::notSupported;
	}



	virtual ResultStatus ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return ResultStatus::notSupported;
	}



	virtual ResultStatus WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return ResultStatus::notSupported;
	}



	virtual ResultStatus ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return ResultStatus::notSupported;
	}



protected:
	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if(status != ResultStatus::ok) {
			return status;
		}

		if(inputBusClockHz != HsiClockHz) {
			return ResultStatus::notSupported;
		}

		uint32 timing = 0;
		if(parameters.speed == Speed::Fast) {
			timing = TimingFast16MHz;
		} else if(parameters.speed == Speed::Standard) {
			timing = TimingStandard16MHz;
		} else {
			return ResultStatus::notSupported;
		}

		// LL_I2C_Init writes OAR1 as a raw register value: the 7-bit address goes to bits 7:1.
		LL_I2C_InitTypeDef init = {
			.PeripheralMode = LL_I2C_MODE_I2C,
			.Timing = timing,
			.AnalogFilter = parameters.analogFilter ? LL_I2C_ANALOGFILTER_ENABLE : LL_I2C_ANALOGFILTER_DISABLE,
			.DigitalFilter = parameters.digitalFilter,
			.OwnAddress1 = static_cast<uint32>(parameters.slaveAddress) << 1,
			.TypeAcknowledge = LL_I2C_ACK,
			.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT
		};

		if(LL_I2C_Init(i2cHandle, &init) != SUCCESS) {
			return ResultStatus::error;
		}

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



private:
	inline void SlaveIrqEvent() {
		if(LL_I2C_IsActiveFlag_ADDR(i2cHandle)) {
			SlaveAddressMatch();
			return;
		}

		if(LL_I2C_IsActiveFlag_TXIS(i2cHandle)) {
			uint8 data = CallSlaveWrite();
			LL_I2C_TransmitData8(i2cHandle, data);
			return;
		}

		if(LL_I2C_IsActiveFlag_RXNE(i2cHandle)) {
			uint8 data = LL_I2C_ReceiveData8(i2cHandle);
			CallSlaveRead(data);
			return;
		}

		if(LL_I2C_IsActiveFlag_NACK(i2cHandle)) {
			// Normal end of a master read: the master NACKs the last byte.
			LL_I2C_ClearFlag_NACK(i2cHandle);
			return;
		}

		if(LL_I2C_IsActiveFlag_STOP(i2cHandle)) {
			SlaveStop();
			return;
		}
	}



	inline void SlaveAddressMatch() {
		// LL_I2C_GetAddressMatchCode returns the matched address shifted left by one (R/W bit excluded).
		uint32 matchCode = LL_I2C_GetAddressMatchCode(i2cHandle) >> 1;
		if(matchCode != parameters.slaveAddress) {
			LL_I2C_ClearFlag_ADDR(i2cHandle);
			CallError(Error::SlaveAddressMatch);
			return;
		}

		slaveTransferIsRead = (LL_I2C_GetTransferDirection(i2cHandle) == LL_I2C_DIRECTION_READ);

		if(slaveTransferIsRead) {
			LL_I2C_DisableIT_RX(i2cHandle);
			LL_I2C_EnableIT_TX(i2cHandle);
		} else {
			LL_I2C_DisableIT_TX(i2cHandle);
			LL_I2C_EnableIT_RX(i2cHandle);
		}

		// Called before ADDR is cleared, so the application resets its TX index before the first TXIS.
		CallSlaveAddressMatch(slaveTransferIsRead);
		LL_I2C_ClearFlag_ADDR(i2cHandle);
	}



	inline void SlaveStop() {
		LL_I2C_DisableIT_TX(i2cHandle);
		LL_I2C_DisableIT_RX(i2cHandle);
		// Drops a byte preloaded into TXDR that the master did not clock out.
		LL_I2C_ClearFlag_TXE(i2cHandle);
		LL_I2C_ClearFlag_STOP(i2cHandle);

		CallSlaveStop(slaveTransferIsRead);
		CallSlaveEndTransfer();
	}



	inline void SlaveAbortTransfer() {
		LL_I2C_DisableIT_TX(i2cHandle);
		LL_I2C_DisableIT_RX(i2cHandle);
		LL_I2C_ClearFlag_TXE(i2cHandle);
		slaveTransferIsRead = false;
	}
};
