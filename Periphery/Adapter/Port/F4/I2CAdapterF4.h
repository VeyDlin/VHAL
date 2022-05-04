#pragma once
#include <System/System.h>
#include <System/SystemUtilities.h>
#include "../../I2CAdapter.h"

using namespace SystemUtilities;


using AI2C = class I2CAdapterF4;



class I2CAdapterF4 : public I2CAdapter {
public:
	uint8 I2C_OK            =  0;
	uint8 I2C_TIME_OUT      =  -1;
	uint8 I2C_BUSY          =  -2;
	uint8 I2C_WRONG_START   =  -3;
	uint8 I2C_ERROR         =  -4;
	uint8 I2C_NOT_ADDRESS   =  -5;
	uint8 I2C_RXNE_ERROR    =  -6;
	uint8 I2C_AF_ERROR      =  -7;
	uint8 I2C_STOPF_ERROR   =  -8;
	uint8 I2C_BTF_ERROR     =  -9;
	uint8 I2C_TXNE_ERROR    =  -10;
	uint8 I2C_SB_ERROR      =  -11;
	uint8 I2C_END           =  -20;



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

	}





	virtual Status::statusType CheckDeviceAsync(uint8 deviceAddress, uint16 repeat) override {
		return Status::notSupported;
	}



	virtual Status::info<uint8> Scan(uint8 *listBuffer, uint8 size) override {
		uint8 count = 0;
	/*	for (uint8 i = 0; i < 127 && i <= size; i++) {
			if (CheckDevice(i, 1) == Status::ok) {
				*listBuffer = i;
				listBuffer++;
				count++;
			}
		}*/
		return { Status::notSupported };
	}





	virtual Status::info<uint8> ScanAsync(uint8 *listBuffer, uint8 size) override {
		return { Status::notSupported };
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

	}





	virtual Status::statusType WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return Status::notSupported;
	}

	virtual Status::statusType ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return Status::notSupported;
	}



private:


	Status::statusType _SendSlaveAddress(uint8_t DevAddress, uint16_t TimeOut) {
		uint32_t I2C_Time;

		// Устанавливаем бит ACK
		i2cHandle->CR1 |= I2C_CR1_ACK;
		// Устанавливаем бит START
		i2cHandle->CR1 |= I2C_CR1_START;
		// Ждём сброса SB
		I2C_Time = System::GetTick();
		while (!(i2cHandle->SR1 & I2C_SR1_SB)) {
			if (System::GetTick() > (I2C_Time + TimeOut)) {
				if (i2cHandle->CR1 & I2C_CR1_START)
					return Status::error;            // Похоже старт затянулся
			}
		}
		// Если адрес 7 бит пуляем его
		i2cHandle->DR = (uint8_t)(DevAddress << 1) & ~1U; // Выплёвываем адрес со сброшенным битом RD
		// Если 10бит оставляем место для пуляния
		// Ждём флага ADDR
		I2C_Time = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_ADDR) == 0)
				&& ((i2cHandle->SR1 & I2C_SR1_AF) == 0)) // Пока нет флагов, ждём
		{
			if (System::GetTick() > (I2C_Time + TimeOut))
				break;                                       // Надоело, выходим
		}

		if (i2cHandle->SR1 & I2C_SR1_ADDR)       // Если флаг ADDR всё таки был,
		{
			i2cHandle->CR1 |= I2C_CR1_STOP;                     // Посылаем STOP
			(void) i2cHandle->SR1;                               // Чистим флаги
			(void) i2cHandle->SR2;

			I2C_Time = System::GetTick();
			while (i2cHandle->SR2 & I2C_SR2_BUSY) {
				if (System::GetTick() > (I2C_Time + TimeOut))
					return Status::timeout; // Если флаг BUSY установлен, а время вышло, это ошибка
			}
			return Status::ok;
		} else {
			i2cHandle->CR1 |= I2C_CR1_STOP; // Флага ADDR не было, посылаем STOP
			(void) i2cHandle->SR1;                               // Чистим флаги
			(void) i2cHandle->SR2;

			I2C_Time = System::GetTick();
			while (i2cHandle->SR2 & I2C_SR2_BUSY) {
				if (System::GetTick() > (I2C_Time + TimeOut))
					return Status::timeout; // Если флаг BUSY установлен, а время вышло, это ошибка
			}
			return Status::error;
		}
		return Status::ok;
	}





	Status::statusType _RequestDataRead(uint8_t DevAddress, uint16_t TimeOut) {
		//  uint32_t  I2C_Time;
		Status::statusType Status;

		i2cHandle->CR1 |= I2C_CR1_ACK;                           // Включаем ACK
		i2cHandle->CR1 |= I2C_CR1_START;                         // Выдаём старт

		// Проверяем SB и START
		if (_isSB_Flag(RESET, TimeOut)) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START)
				return Status::timeout;
		}
		// Загоняем адрес на линию в режиме записи
		i2cHandle->DR = (uint8_t)(DevAddress << 1) & ~1U; // Выплёвываем адрес со сброшенным битом RD

		// Проверяем флаг ADDR и AF
		Status = _isAddressAF_Flag(TimeOut);
		if (Status != Status::ok)
			return Status;

		// Очищаем флаги
		(void) i2cHandle->SR1;
		(void) i2cHandle->SR2;

		i2cHandle->CR1 |= I2C_CR1_START;                         // Выдаём старт
		// Проверяем SB и START
		if (_isSB_Flag(RESET, TimeOut)) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START)
				return Status::timeout;
		}

		i2cHandle->DR = (uint8_t)(DevAddress << 1) | 1U; // Выплёвываем адрес с установленным битом RD
		// Проверяем флаг ADDR и AF
		Status = _isAddressAF_Flag(TimeOut);
		if (Status != Status::ok)
			return Status;
		//    }
		return Status::ok;
	}





	Status::statusType _RequestDataWrite(uint8_t DevAddress, uint16_t TimeOut) {
		Status::statusType Status;
		i2cHandle->CR1 |= I2C_CR1_START;                      // Запускаем START
		if (_isSB_Flag(RESET, TimeOut) != I2C_OK) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START) {
				return Status::error;
			}
			return Status::timeout;
		}

		i2cHandle->DR = (DevAddress << 1) & ~1U; // Выдаём на шину адрес устройства
		// Проверяем флаг ADDR и AF
		Status = _isAddressAF_Flag(TimeOut);
		if (Status != Status::ok)
			return Status;

		return Status::ok;
	}





	Status::statusType _RequestMemoryRead(uint8_t DevAddress, uint16_t MemAddress, uint8_t MemAddSise, uint16_t TimeOut) {
		//  uint32_t  I2C_Time;
		Status::statusType Status;

		i2cHandle->CR1 |= I2C_CR1_ACK;                           // Включаем ACK
		i2cHandle->CR1 |= I2C_CR1_START;                         // Выдаём старт

		// Проверяем SB и START
		if (_isSB_Flag(RESET, TimeOut)) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START)
				return Status::timeout;
		}
		// Загоняем адрес на линию в режиме записи
		i2cHandle->DR = (uint8_t)(DevAddress << 1) & ~1U; // Выплёвываем адрес со сброшенным битом RD

		// Проверяем флаг ADDR и AF
		Status = _isAddressAF_Flag(TimeOut);
		if (Status != Status::ok)
			return Status;

		// Очищаем флаги
		(void) i2cHandle->SR1;
		(void) i2cHandle->SR2;

		// Ждём когда поднимется TXE
		Status = _isTXE_Flag(TimeOut);
		if (Status != Status::ok) {
			if (Status == I2C_AF_ERROR)
				i2cHandle->CR1 |= I2C_CR1_STOP;
			return Status;
		}

		// Закидываем 8 или 16 бит адрес
		if (MemAddSise == 1) {
			i2cHandle->DR = MemAddress & 0xFF;              // Пихаем 8бит адрес
		} else {
			i2cHandle->DR = (MemAddress >> 8) & 0xFF; // Пихаем старшие 8 бит адрес
			// Ждём когда поднимется TXE
			Status = _isTXE_Flag(TimeOut);
			if (Status != Status::ok) {
				if (Status == Status::error)
					i2cHandle->CR1 |= I2C_CR1_STOP;
				return Status;
			}
			i2cHandle->DR = MemAddress & 0xFF;           // Пихаем младшие 8 бит
			// Ждём когда поднимется TXE
			Status = _isTXE_Flag(TimeOut);
			if (Status != Status::ok) {
				if (Status == Status::error)
					i2cHandle->CR1 |= I2C_CR1_STOP;
				return Status;
			}

			i2cHandle->CR1 |= I2C_CR1_START;                     // Выдаём старт
			// Проверяем SB и START
			if (_isSB_Flag(RESET, TimeOut)) {
				if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START)
					return Status::error;
			}

			i2cHandle->DR = (uint8_t)(DevAddress << 1) | 1U; // Выплёвываем адрес с установленным битом RD
			// Проверяем флаг ADDR и AF
			Status = _isAddressAF_Flag(TimeOut);
			if (Status != Status::ok)
				return Status;
		}
		return Status::ok;
	}





	Status::statusType _RequestMemoryWrite(uint8_t DevAddress, uint16_t MemAddress, uint8_t MemAddSise, uint16_t TimeOut) {
		Status::statusType Status;
		i2cHandle->CR1 |= I2C_CR1_START;                      // Запускаем START
		if (_isSB_Flag(RESET, TimeOut) != Status::ok) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START) {
				return Status::error;
			}
			return Status::timeout;
		}

		i2cHandle->DR = (DevAddress << 1) & ~1U; // Выдаём на шину адрес устройства
		// Проверяем флаг ADDR и AF
		Status = _isAddressAF_Flag(TimeOut);
		if (Status != Status::ok)
			return Status;

		// Очищаем флаги
		(void) i2cHandle->SR1;
		(void) i2cHandle->SR2;

		// Ждём когда поднимется TXE
		Status = _isTXE_Flag(TimeOut);
		if (Status != Status::ok) {
			if (Status == Status::error)
				i2cHandle->CR1 |= I2C_CR1_STOP;
			return Status;
		}

		// Закидываем 8 или 16 бит адрес
		if (MemAddSise == 1) {
			i2cHandle->DR = MemAddress & 0xFF;              // Пихаем 8бит адрес
		} else {
			i2cHandle->DR = (MemAddress >> 8) & 0xFF; // Пихаем старшие 8 бит адрес
			// Ждём когда поднимется TXE
			Status = _isTXE_Flag(TimeOut);
			if (Status != Status::ok) {
				if (Status == Status::error)
					i2cHandle->CR1 |= I2C_CR1_STOP;
				return Status;
			}
			i2cHandle->DR = MemAddress & 0xFF;           // Пихаем младшие 8 бит
			// Ждём когда поднимется TXE
			Status = _isTXE_Flag(TimeOut);
			if (Status != Status::ok) {
				if (Status == Status::error)
					i2cHandle->CR1 |= I2C_CR1_STOP;
				return Status;
			}
		}
		return Status::ok;
	}





	Status::statusType _isBusyFlag(bool FlagSet, uint16_t TimeOut) {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR2 & I2C_SR2_BUSY) >> I2C_SR2_BUSY_Pos) == FlagSet) {
			if ((System::GetTick() - TickStart) > TimeOut) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isSB_Flag(bool FlagSet, uint16_t TimeOut) {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_SB) >> I2C_SR1_SB_Pos) == FlagSet) {
			if ((System::GetTick() - TickStart) > TimeOut) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isAddressAF_Flag(uint16_t TimeOut) {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_ADDR) >> I2C_SR1_ADDR_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_AF) >> I2C_SR1_AF_Pos) == SET) {
				i2cHandle->CR1 |= I2C_CR1_STOP;
				i2cHandle->SR1 &= ~I2C_SR1_AF;
				return Status::error;
			}

			if ((System::GetTick() - TickStart) > TimeOut) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isTXE_Flag(uint16_t TimeOut) {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_TXE) >> I2C_SR1_TXE_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_AF) >> I2C_SR1_AF_Pos) == SET) {
				i2cHandle->SR1 &= ~I2C_SR1_AF;
				return Status::error;
			}

			if ((System::GetTick() - TickStart) > TimeOut) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isRXNE_Flag(uint16_t TimeOut) {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_RXNE) >> I2C_SR1_RXNE_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_STOPF) >> I2C_SR1_STOPF_Pos)
					== SET) {
				i2cHandle->SR1 &= ~I2C_SR1_STOPF;
				return Status::error;
			}

			if ((System::GetTick() - TickStart) > TimeOut) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isBTF_Flag(bool FlagSet, uint16_t TimeOut) {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_BTF) >> I2C_SR1_BTF_Pos) == FlagSet) {
			if ((System::GetTick() - TickStart) > TimeOut) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isBTF_Flag_TimeOut(uint16_t TimeOut) {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_BTF) >> I2C_SR1_BTF_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_AF) >> I2C_SR1_AF_Pos) != I2C_OK)
				return Status::error;

			if ((System::GetTick() - TickStart) > TimeOut) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}
};















