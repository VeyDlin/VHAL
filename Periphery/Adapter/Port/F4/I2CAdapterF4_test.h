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
        if (LL_I2C_IsActiveFlag_SB(i2cHandle)) {
            // Был выдан стартовый бит.
            // Флаг снимается после чтения SR1 (уже сделано в
            // начале функции) и записи DR (сделано прямо сейчас)
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

            data->state = I2C_SENDING_ADDRESS;
        }

        if (LL_I2C_IsActiveFlag_ADDR(i2cHandle) && data->state == I2C_SENDING_ADDRESS) {
            // Основной режим: Отправлен адресный байт
            // Флаг снимается после считывания как SR1, так и SR2

            if (data->len_send > 0) {
                // Снимите флажок, прочитав SR2. Это необходимо сделать перед отправкой данных
                sr2 = *i2c_get_SR2(_i2c);

                LL_I2C_TransmitData8(txDataPointer[txDataCounter++]);

                if (txDataCounter == txDataNeed) {
                    if (rxDataNeed == 0) {
                        LL_I2C_GenerateStopCondition(i2cHandle);
                        tx_rx_end(_i2c, I2C_IDLE); // The transfer is complete
                    } else {
                        LL_I2C_GenerateStartCondition(i2cHandle);
                        data->state = I2C_SENDING_RESTART;
                    }

                    // Indicate that there is nothing else to send
                    txDataNeed = 0;
                    txDataCounter = 0;
                } else {
                    data->state = I2C_SENDING_DATA;

                    // STMF: не включайте ITBUFEN, так как мы не можем гарантировать
                    // для обработки EV8 перед передачей текущего байта после TXNE
                    // вместо этого мы будем ждать BTF (который снижает скорость передачи)
                }
            } else {
                switch (rxDataNeed) {
                    case 1:
                        // STM32F_errata:
                        // + the sequence read_SR2 STOP should be fast enough
                        // + or SCL must be tied low in during it

                        LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK);

                        // Clear the flag by reading SR2
                        sr2 = *i2c_get_SR2(_i2c);

                        LL_I2C_GenerateStopCondition(i2cHandle);
                        LL_I2C_EnableIT_BUF(i2cHandle);
                    break;

                    case 2:
                        // STM32F_errata:
                        // + the sequence read_SR2 NACK should be fast enough
                        // + or SCL must be tied low in during it

                        // Clear the flag by reading SR2
                        sr2 = *i2c_get_SR2(_i2c);

                        LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK);
                    break;

                    default:
                        // В случае по умолчанию нам не нужно ничего делать
                        // за исключением снятия флага путем чтения SR2
                        sr2 = *i2c_get_SR2(_i2c);
                    break;
                }

                data->state = I2C_RECEIVING_DATA;
            }
            return;
        }
#ifdef I2C__SLAVE_SUPPORT
        if (LL_I2C_IsActiveFlag_ADDR(i2cHandle) && (data->state == I2C_IDLE || (data->state == I2C_SL_RX && !LL_I2C_IsActiveFlag_RXNE(i2cHandle)))) {
            // Clear the flag by reading SR2.
            sr2 = *i2c_get_SR2(_i2c);

            // Disable Buffer Interrupt
            LL_I2C_DisableIT_BUF(i2cHandle);

            if (LL_I2C_GetTransferDirection() == LL_I2C_DIRECTION_READ) {
                data->state = I2C_SL_TX;
                auto data = CallSlaveWrite();
                LL_I2C_TransmitData8(i2cHandle, data);
            } else {
                data->state = I2C_SL_RX;
            }

            return;
        }
#endif

        if (LL_I2C_IsActiveFlag_ADD10(i2cHandle)) {
            // This should not happen as we do not handle 10-bits addresses
            LL_I2C_GenerateStopCondition(i2cHandle);
            tx_rx_end(_i2c, I2C_ERROR);
            return;
        }

        if (LL_I2C_IsActiveFlag_RXNE(i2cHandle)) {
            if (data->state == I2C_RECEIVING_DATA) {
                int remaining = rxDataNeed - rxDataCounter;
                if (remaining <= 0) {
                    // should not happen
                    LL_I2C_GenerateStopCondition(i2cHandle);
                    LL_I2C_ReceiveData8();
                    LL_I2C_ReceiveData8();
                    tx_rx_end(_i2c, I2C_ERROR);
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
                                // STM32F_errata:
                                // + the sequence STOP read_DR read_DR should be fast enough
                                // + or SCL must be tied low in during it

                                LL_I2C_GenerateStopCondition(i2cHandle);
                                rxDataPointer[rxDataCounter++] = LL_I2C_ReceiveData8(i2cHandle);
                                rxDataPointer[rxDataCounter++] = LL_I2C_ReceiveData8(i2cHandle);

                                rxDataNeed = 0;
                                rxDataCounter = 0;

                                tx_rx_end(_i2c, I2C_IDLE);
                            }
                        break;

                        case 1:
                            rxDataPointer[rxDataCounter++] = LL_I2C_ReceiveData8(i2cHandle);

                            rxDataNeed = 0;
                            rxDataCounter = 0;

                            tx_rx_end(_i2c, I2C_IDLE);
                        break;
                    }
                }
            }
#ifdef I2C__SLAVE_SUPPORT
            else if (data->state == I2C_SL_RX) {
                if (LL_I2C_IsActiveFlag_BTF(i2cHandle) || (LL_I2C_IsActiveFlag_ADDR(i2cHandle) && LL_I2C_IsActiveFlag_TXE(i2cHandle))) {
                    auto data = LL_I2C_ReceiveData8(i2cHandle);
                    CallSlaveRead(data);
                }
            }
#endif
        }

        if (LL_I2C_IsActiveFlag_TXE(i2cHandle)) {
            if (data->state == I2C_SENDING_DATA) {
                if (txDataNeed == txDataCounter) {
                    if (rxDataNeed == 0) {
                        LL_I2C_GenerateStopCondition(i2cHandle);
                        tx_rx_end(_i2c, I2C_IDLE);
                    } else {
                        LL_I2C_GenerateStartCondition(i2cHandle);
                        data->state = I2C_SENDING_RESTART;
                    }

                    txDataNeed = 0;
                    txDataCounter = 0;
                } else {
                    LL_I2C_TransmitData8(txDataPointer[txDataCounter++]);
                }
            }
#ifdef I2C__SLAVE_SUPPORT
            else if (data->state == I2C_SL_TX) {
                auto data = CallSlaveWrite();
                LL_I2C_TransmitData8(i2cHandle, data);
            }
#endif
        }


        if (LL_I2C_IsActiveFlag_STOP(i2cHandle)) {
            switch (data->state) {
#ifdef I2C__SLAVE_SUPPORT
                case I2C_SL_RX:
                    if (LL_I2C_IsActiveFlag_RXNE(i2cHandle)) {
                        auto data = LL_I2C_ReceiveData8(i2cHandle);
                        CallSlaveRead(data);
                    }
                    LL_I2C_ClearFlag_STOP(i2cHandle);
                    data->state = I2C_IDLE;
                    if (data->slave_handler) {
                        data->slave_handler(I2C_SLAVE_EV_STOP, NULL);
                    }
                break;

                case I2C_SL_TX:
                    CallError(Error::None); // TODO: STOPF interrupt
                    data->state = I2C_IDLE;
                break;
#endif
                default:
                    // This should not happen as we are in master mode
                    CallError(Error::None); // TODO: STOPF interrupt
                    LL_I2C_ClearFlag_ADDR();

                    LL_I2C_GenerateStopCondition(i2cHandle);
    `
                    tx_rx_end(_i2c, I2C_ERROR);
                break;
            }
        }
	}



	virtual void IrqErrorHandler() override {
        uint16_t sr1 = *i2c_get_SR1(_i2c);
        uint16_t sr2 = *i2c_get_SR2(_i2c);
        (void)sr2; // clear defined but not used warning warning

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
            if (_i2c->data->state == I2C_SL_TX) {
                LL_I2C_ClearFlag_AF(i2cHandle);
                _i2c->data->state = I2C_IDLE;
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

        tx_rx_end(_i2c, I2C_ERROR);
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

		EnableI2C();
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
		return Status::notSupported;
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







    inline void TransmitData(uint8 count = 1) {
        while (count-- > 0) {
            LL_I2C_TransmitData8(*txDataPointer++);
            txDataCounter++;
        }
    }

	inline void ReceiveData(uint8 count = 1) {
		while(count-- > 0) {
			*rxDataPointer++ = LL_I2C_ReceiveData8(i2cHandle);
			rxDataCounter++;
		}
	}


	void EnableI2C() {
		if (!LL_I2C_IsEnabled(i2cHandle)) {
			LL_I2C_Enable(i2cHandle);
		}
	}

};















