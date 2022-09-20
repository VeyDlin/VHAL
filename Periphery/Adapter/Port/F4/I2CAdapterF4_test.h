#pragma once
#include <System/System.h>
#include <System/SystemUtilities.h>
#include "../../I2CAdapter.h"

using namespace SystemUtilities;


using AI2C = class I2CAdapterF4;



class I2CAdapterF4 : public I2CAdapter {
private:
    enum class StateI2C {
        Idle,
        SendingStart,
        SendingAddress,
        WriteData,
        ReadData,
        SendingRestart,
        SlaveWrite,
        SlaveRead,
        Error
    } stateI2C;


public:
	I2CAdapterF4() { }
	I2CAdapterF4(I2C_TypeDef *i2c, uint32 busClockHz):I2CAdapter(i2c, busClockHz) { }



	virtual void IrqEventHandler() override {
        if (LL_I2C_IsActiveFlag_SB(i2cHandle)) {
            if (txDataNeed == 0) {
                LL_I2C_TransmitData8(deviceAddress | 0x01);
                if (rxDataNeed == 2) {
                    LL_I2C_EnableBitPOS(i2cHandle);
                }
            } else {
                LL_I2C_TransmitData8(deviceAddress & ~0x01); // TODO: & ~0x01 ?
            }

            LL_I2C_DisableIT_BUF(i2cHandle);
            LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_ACK);

            stateI2C = StateI2C::SendingAddress;
        }



        if (LL_I2C_IsActiveFlag_ADDR(i2cHandle) && stateI2C == StateI2C::SendingAddress) {
            LL_I2C_ClearFlag_ADDR(i2cHandle);

            if (txDataNeed > 0) {
                LL_I2C_TransmitData8(txDataPointer[txDataCounter++]);

                if (txDataCounter != txDataNeed) {
                    stateI2C = StateI2C::WriteData;
                    return;
                }
                
                if (rxDataNeed == 0) {
                    LL_I2C_GenerateStopCondition(i2cHandle);
                    IrqTransferCompleted(StateI2C::Idle);
                } else {
                    LL_I2C_GenerateStartCondition(i2cHandle);
                    stateI2C = StateI2C::SendingRestart;
                }

                txDataNeed = 0;
                txDataCounter = 0;
                return;
            } 


            switch (rxDataNeed) {
                case 1:
                    LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK);
                    LL_I2C_ClearFlag_ADDR(i2cHandle);
                    LL_I2C_GenerateStopCondition(i2cHandle);
                    LL_I2C_EnableIT_BUF(i2cHandle);
                break;

                case 2:
                    LL_I2C_ClearFlag_ADDR(i2cHandle);
                    LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK);
                break;

                default:
                    LL_I2C_ClearFlag_ADDR(i2cHandle);
                break;
            }

            stateI2C = StateI2C::ReadData;
            return;
        }



#ifdef I2C__SLAVE_SUPPORT
        if (LL_I2C_IsActiveFlag_ADDR(i2cHandle) && (stateI2C == StateI2C::Idle || (stateI2C == StateI2C::SlaveRead && !LL_I2C_IsActiveFlag_RXNE(i2cHandle)))) {
            LL_I2C_ClearFlag_ADDR(i2cHandle);

            LL_I2C_DisableIT_BUF(i2cHandle);

            if (LL_I2C_GetTransferDirection() == LL_I2C_DIRECTION_READ) {
                stateI2C = StateI2C::SlaveWrite;
                auto data = CallSlaveWrite();
                LL_I2C_TransmitData8(i2cHandle, data);
            } else {
                stateI2C = StateI2C::SlaveRead;
            }

            return;
        }
#endif


        if (LL_I2C_IsActiveFlag_ADD10(i2cHandle)) {
            // This should not happen as we do not handle 10-bits addresses
            LL_I2C_GenerateStopCondition(i2cHandle);
            IrqTransferCompleted(StateI2C::Error);
            return;
        }



        if (LL_I2C_IsActiveFlag_RXNE(i2cHandle)) {
            if (stateI2C == StateI2C::ReadData) {
                int remaining = rxDataNeed - rxDataCounter;
                if (remaining <= 0) {
                    // should not happen
                    LL_I2C_GenerateStopCondition(i2cHandle);
                    LL_I2C_ReceiveData8();
                    LL_I2C_ReceiveData8();
                    IrqTransferCompleted(StateI2C::Error);
                } else {
                    switch (remaining) {
                        default:// >= 4
                            if (LL_I2C_IsActiveFlag_BTF(i2cHandle)) {
                                rxDataPointer[rxDataCounter++] = LL_I2C_ReceiveData8(i2cHandle);
                            }
                        break;

                        case 3:
                            if (LL_I2C_IsActiveFlag_BTF(i2cHandle)) {
                                LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK);
                                rxDataPointer[rxDataCounter++] = LL_I2C_ReceiveData8(i2cHandle);
                            }
                        break;

                        case 2:
                            if (LL_I2C_IsActiveFlag_BTF(i2cHandle)) {
                                LL_I2C_GenerateStopCondition(i2cHandle);
                                rxDataPointer[rxDataCounter++] = LL_I2C_ReceiveData8(i2cHandle);
                                rxDataPointer[rxDataCounter++] = LL_I2C_ReceiveData8(i2cHandle);

                                rxDataNeed = 0;
                                rxDataCounter = 0;

                                IrqTransferCompleted(StateI2C::Idle);
                            }
                        break;

                        case 1:
                            rxDataPointer[rxDataCounter++] = LL_I2C_ReceiveData8(i2cHandle);

                            rxDataNeed = 0;
                            rxDataCounter = 0;

                            IrqTransferCompleted(StateI2C::Idle);
                        break;
                    }
                }
            }
#ifdef I2C__SLAVE_SUPPORT
            else if (stateI2C == StateI2C::SlaveRead) {
                if (LL_I2C_IsActiveFlag_BTF(i2cHandle) || (LL_I2C_IsActiveFlag_ADDR(i2cHandle) && LL_I2C_IsActiveFlag_TXE(i2cHandle))) {
                    auto data = LL_I2C_ReceiveData8(i2cHandle);
                    CallSlaveRead(data);
                }
            }
#endif
        }



        if (LL_I2C_IsActiveFlag_TXE(i2cHandle)) {
            if (stateI2C == StateI2C::WriteData) {
                if (txDataNeed == txDataCounter) {
                    if (rxDataNeed == 0) {
                        LL_I2C_GenerateStopCondition(i2cHandle);
                        IrqTransferCompleted(StateI2C::Idle);
                    } else {
                        LL_I2C_GenerateStartCondition(i2cHandle);
                        stateI2C = StateI2C::SendingRestart;
                    }

                    txDataNeed = 0;
                    txDataCounter = 0;
                } else {
                    LL_I2C_TransmitData8(txDataPointer[txDataCounter++]);
                }
            }
#ifdef I2C__SLAVE_SUPPORT
            else if (stateI2C == StateI2C::SlaveWrite) {
                auto data = CallSlaveWrite();
                LL_I2C_TransmitData8(i2cHandle, data);
            }
#endif
        }



        if (LL_I2C_IsActiveFlag_STOP(i2cHandle)) {
            switch (stateI2C) {
#ifdef I2C__SLAVE_SUPPORT
                case StateI2C::SlaveRead:
                    if (LL_I2C_IsActiveFlag_RXNE(i2cHandle)) {
                        auto data = LL_I2C_ReceiveData8(i2cHandle);
                        CallSlaveRead(data);
                    }
                    LL_I2C_ClearFlag_STOP(i2cHandle);
                    stateI2C = StateI2C::Idle;
                    if (data->slave_handler) {
                        data->slave_handler(I2C_SLAVE_EV_STOP, NULL);
                    }
                break;

                case StateI2C::SlaveWrite:
                    CallError(Error::None); // TODO: STOPF interrupt
                    stateI2C = StateI2C::Idle;
                break;
#endif
                default:
                    // This should not happen as we are in master mode
                    CallError(Error::None); // TODO: STOPF interrupt
                    LL_I2C_ClearFlag_ADDR();

                    LL_I2C_GenerateStopCondition(i2cHandle);
    `
                    IrqTransferCompleted(StateI2C::Error);
                break;
            }
        }
	}





	virtual void IrqErrorHandler() override {
        if (LL_I2C_IsActiveFlag_BERR(i2cHandle)) {
            CallError(Error::None); // TODO: Bus Error
            LL_I2C_ClearFlag_BERR(i2cHandle);
        }

        if (LL_I2C_IsActiveFlag_ARLO(i2cHandle)) {
            CallError(Error::None); // TODO: Arbitration Lost
            LL_I2C_ClearFlag_ARLO(i2cHandle);
        }

        if (LL_I2C_IsActiveFlag_AF(i2cHandle)) {
#ifdef I2C__SLAVE_SUPPORT
            if (_i2c->stateI2C == StateI2C::SlaveWrite) {
                LL_I2C_ClearFlag_AF(i2cHandle);
                _i2c->stateI2C = StateI2C::Idle;
                if (_i2c->data->slave_handler) {
                    _i2c->data->slave_handler(I2C_SLAVE_EV_STOP, NULL);
                }
                return;
            }
#endif
            CallError(Error::None); // TODO: Acknowledge Failure
            LL_I2C_ClearFlag_AF(i2cHandle);
        }

        if (LL_I2C_IsActiveFlag_OVR(i2cHandle)) {
            CallError(Error::None); // TODO: Overrun
            LL_I2C_ClearFlag_OVR(i2cHandle);
        }

        if (LL_I2C_IsActiveSMBusFlag_PECERR(i2cHandle)) {
            CallError(Error::None); // TODO: PEC error
            LL_I2C_ClearSMBusFlag_PECERR(i2cHandle);
        }

        if (LL_I2C_IsActiveSMBusFlag_TIMEOUT(i2cHandle)) {
            CallError(Error::None); // TODO: Timeout
            LL_I2C_ClearSMBusFlag_TIMEOUT(i2cHandle);
        }

        if (LL_I2C_IsActiveSMBusFlag_ALERT(i2cHandle)) {
            CallError(Error::None); // TODO: SMBAlert
            LL_I2C_ClearSMBusFlag_ALERT(i2cHandle);
        }

        LL_I2C_GenerateStopCondition(i2cHandle);

        IrqTransferCompleted(StateI2C::Error);
	}



public:
	virtual Status::statusType StartSlaveListen() {
		return Status::notSupported;
	}


	virtual Status::statusType StopSlaveListen() {
		return Status::notSupported;
	}





	virtual Status::statusType CheckDevice(uint8 deviceAddress, uint16 repeat) override {
		return Status::notSupported;
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
        uint8 count = 0;
        for (uint8 i = 0; i < 127 && i <= size; i++) {
            CheckDeviceAsync(i, 1);
            if (Await() == Status::ok) {
                *listBuffer = i;
                listBuffer++;
                count++;
            }
        }
        return count;
	}





    virtual Status::statusType Await() override {
        auto statusAwait = I2CAdapter::Await();
        while (LL_I2C_IsActiveFlag_BUSY(i2cHandle)); // TODO: add timeout
        return statusAwait;
    }



protected:
	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		LL_I2C_InitTypeDef init = {
            .PeripheralMode = LL_I2C_MODE_I2C,
            .ClockSpeed = static_cast<uint16>(parameters.speed),
            .DutyCycle = LL_I2C_DUTYCYCLE_2,
            .OwnAddress1 = parameters.slaveAddress,
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
		return Status::notSupported;
	}

	virtual Status::statusType WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return WriteReadStartAsync(device, address, addressSize, writeData, dataSize, nullptr, 0);
	}

	virtual Status::statusType ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return WriteReadStartAsync(device, address, addressSize, nullptr, 0, readData, dataSize);
	}



private:
    Status::statusType WriteReadStartAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 writeDataSize, uint8* readData, uint32 readDataSize) {
        if (state != Status::ready && state != Status::error) {
            return Status::busy;
        }
        
        state = Status::busy;

        deviceAddress = device;
        registerAddress = address;
        registerAddressSize = addressSize;
        rxDataNeed = readDataSize;
        rxDataCounter = 0;
        rxDataPointer = readData;
        txDataNeed = writeDataSize;
        txDataCounter = 0;
        txDataPointer = writeData;

        if (!LL_I2C_IsEnabled(i2cHandle)) {
            LL_I2C_Enable(i2cHandle);
        }
        
        LL_I2C_ClearFlag_ADDR(i2cHandle);

        stateI2C = StateI2C::SendingStart;

        LL_I2C_EnableIT_EVT(i2cHandle);
        LL_I2C_EnableIT_ERR(i2cHandle);

        LL_I2C_DisableBitPOS(i2cHandle);
        LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_ACK);
        LL_I2C_GenerateStartCondition(i2cHandle);

        return Status::ok;
    }



    void IrqTransferCompleted(StateI2C endState) {
        switch (endState) {
            case I2CAdapterF4::StateI2C::Error:
                state = Status::error;
            break;

            case I2CAdapterF4::StateI2C::Idle:
            default:
                state = Status::ready;
            break;
        }

        LL_I2C_DisableIT_EVT(i2cHandle);
        LL_I2C_DisableIT_ERR(i2cHandle);

        stateI2C = StateI2C::Idle;
    }

};















