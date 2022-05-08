#pragma once
#include <System/System.h>
#include <Utilities/WaitCondition/WaitCondition.h>
#include "../../I2CAdapter.h"

using namespace WaitCondition;


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





	virtual Status::statusType CheckDevice(uint8 device, uint16 repeat) override {
		uint8_t I2C_Repeat;
		uint32_t I2C_Time;
		bool Tmp1, Tmp2;

		// Ждём сброса флага BUSY
		I2C_Time = System::GetTick();
		if (_isBusyFlag(SET) != Status::ok) return Status::busy;

		// Включаем передатчик
		if ((i2cHandle->CR1 & I2C_CR1_PE) != I2C_CR1_PE) i2cHandle->CR1 |= I2C_CR1_PE;

		// Отключаем POS
		i2cHandle->CR1 &= ~I2C_CR1_POS;

		do {
			// Стартуем
			i2cHandle->CR1 |= I2C_CR1_START;

			I2C_Time = System::GetTick();
			if (_isSB_Flag(RESET) != Status::ok) return Status::error;

			// Выплёвываем адрес со сброшенным битом RD
			i2cHandle->DR = (uint8_t) (device << 1) & ~1U;

			I2C_Time = System::GetTick();
			Tmp1 = i2cHandle->SR1 & I2C_SR1_ADDR;
			Tmp2 = i2cHandle->SR1 & I2C_SR1_AF;

			// Пока нет флагов, ждём
			while ((Tmp1 == 0) && (Tmp2 == 0)) {
				if (System::GetTick() > (I2C_Time + timeout)) break;
				Tmp1 = i2cHandle->SR1 & I2C_SR1_ADDR;
				Tmp2 = i2cHandle->SR1 & I2C_SR1_AF;
			}

			// Если флаг ADDR всё таки был,
			if (i2cHandle->SR1 & I2C_SR1_ADDR) {
				// Посылаем STOP
				i2cHandle->CR1 |= I2C_CR1_STOP;

				// Чистим флаги
				(void) i2cHandle->SR1;
				(void) i2cHandle->SR2;

				I2C_Time = System::GetTick();
				while (i2cHandle->SR2 & I2C_SR2_BUSY) {
					// Если флаг BUSY установлен, а время вышло, это ошибка
					if (System::GetTick() > (I2C_Time + timeout)) return Status::busy;
				}
				return Status::ok;
			} else {
				// Флага ADDR не было, посылаем STOP
				i2cHandle->CR1 |= I2C_CR1_STOP;

				// Сбрасываем флаг AF
				i2cHandle->SR1 &= ~I2C_SR1_AF;

				I2C_Time = System::GetTick();
				while (i2cHandle->SR2 & I2C_SR2_BUSY) {
					// Если флаг BUSY установлен, а время вышло, это ошибка
					if (System::GetTick() > (I2C_Time + timeout)) return Status::busy;
				}
			}

			I2C_Repeat++;
		} while (I2C_Repeat < repeat);
		return Status::error;
	}





	virtual Status::statusType CheckDeviceAsync(uint8 deviceAddress, uint16 repeat) override {
		return Status::notSupported;
	}



	virtual Status::info<uint8> Scan(uint8 *listBuffer, uint8 size) override {
		uint8 count = 0;
		for (uint8 i = 0; i < 127 && i <= size; i++) {
			if (CheckDevice(i, 1) == Status::ok) {
				*listBuffer = i;
				listBuffer++;
				count++;
			}
		}
		return { Status::ok, count };
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

		// Проверяем занятость устройства
		if (_isBusyFlag(SET) != I2C_OK) return Status::busy;

		// Проверяем, включено ли устройство
		// Включаем передатчик если не включен
		if ((i2cHandle->CR1 & I2C_CR1_PE) != I2C_CR1_PE) i2cHandle->CR1 |= I2C_CR1_PE;

		// Отключаем POS
		i2cHandle->CR1 &= ~I2C_CR1_POS;

		// Обращаемся к функции RequestMemoryWrite
		auto status = _RequestMemoryWrite(device, address, addressSize);

		while (dataSize > 0) {
			if (_isTXE_Flag() == Status::error) {
				i2cHandle->CR1 |= I2C_CR1_STOP;
				return Status::error;
			}
			i2cHandle->DR = (uint8_t) *writeData;
			writeData++;
			dataSize--;

			if ((_isBTF_Flag(SET) != I2C_OK) && (dataSize != 0)) {
				i2cHandle->DR = (uint8_t) *writeData;
				writeData++;
				dataSize--;
			}
		}

		status = _isBTF_Flag_TimeOut();
		if (status != Status::ok) {
			if (status == Status::error) i2cHandle->CR1 |= I2C_CR1_STOP;
			return Status::error;
		}
		i2cHandle->CR1 |= I2C_CR1_STOP;
		return Status::ok;
	}





	virtual Status::statusType ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *readData, uint32 dataSize) override {
		uint16_t Count = dataSize;

		// Ждём сброса флага BUSY
		if (_isBusyFlag(SET) != Status::ok) return Status::busy;

		// Включаем передатчик если не включен
		if ((i2cHandle->CR1 & I2C_CR1_PE) != I2C_CR1_PE) i2cHandle->CR1 |= I2C_CR1_PE;

		// Отключаем POS
		i2cHandle->CR1 &= ~I2C_CR1_POS;

		// Посылаем запрос на чтение памяти
		auto status = _RequestMemoryRead(device, address, addressSize);
		if (status != Status::ok) return status;

		// Проверяем количество передаваемых байт на 0, 1, 2 и более
		if (dataSize == 0) {
			// Сбрасываем все флаги
			(void) (i2cHandle->SR1);
			(void) (i2cHandle->SR2);

			// Генерим STOP
			i2cHandle->CR1 |= I2C_CR1_STOP;
		} else if (dataSize == 1) {
			// Сбрасываем ACK
			i2cHandle->CR1 &= ~I2C_CR1_ACK;

			// Сбрасываем все флаги
			(void) (i2cHandle->SR1);
			(void) (i2cHandle->SR2);

			// Генерим STOP
			i2cHandle->CR1 |= I2C_CR1_STOP;
		} else if (dataSize == 2) {
			// Сбрасываем ACK
			i2cHandle->CR1 &= ~I2C_CR1_ACK;

			// Устанавливаем POS
			i2cHandle->CR1 |= I2C_CR1_POS;

			// Сбрасываем все флаги
			(void) (i2cHandle->SR1);
			(void) (i2cHandle->SR2);
		} else { // Если больше 2х
			// Сбрасываем все флаги
			(void) (i2cHandle->SR1);
			(void) (i2cHandle->SR2);
		}

		// Далее цикл чтения
		while (Count > 0) {

			// Если считываемых байт осталось 3 и менее
			if (Count <= 3) {

				if (Count == 1) {
					if (_isRXNE_Flag() != Status::ok) return Status::error;
					*readData = (uint8_t) i2cHandle->DR;
					readData++;
					dataSize--;
					Count--;
				} else if (Count == 2) {
					if (_isBTF_Flag(RESET) != Status::ok) return Status::error;
					i2cHandle->CR1 |= I2C_CR1_STOP;
					*readData = (uint8_t) i2cHandle->DR;
					readData++;
					dataSize--;
					Count--;
					*readData = (uint8_t) i2cHandle->DR;
					readData++;
					dataSize--;
					Count--;
				} else { // Три байта
					if (_isBTF_Flag(RESET) != Status::ok) return Status::error;
					i2cHandle->CR1 &= ~I2C_CR1_ACK;
					*readData = (uint8_t) i2cHandle->DR;
					readData++;
					dataSize--;
					Count--;
					if (_isBTF_Flag(RESET) != Status::ok) return Status::error;
					i2cHandle->CR1 |= I2C_CR1_STOP;
					*readData = (uint8_t) i2cHandle->DR;
					readData++;
					dataSize--;
					Count--;
					*readData = (uint8_t) i2cHandle->DR;
					readData++;
					dataSize--;
					Count--;
				}
			} else { // Если считываемых байт ещё больше 3х
				if (_isRXNE_Flag() != Status::ok) return Status::error;
				*readData = (uint8_t) i2cHandle->DR;
				readData++;
				dataSize--;
				Count--;
				if (((i2cHandle->SR1 & I2C_SR1_BTF) >> I2C_SR1_BTF_Pos) == SET) {
					*readData = (uint8_t) i2cHandle->DR;
					readData++;
					dataSize--;
					Count--;
				}
			}
		}
		return Status::ok;
	}





	virtual Status::statusType WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return Status::notSupported;
	}

	virtual Status::statusType ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return Status::notSupported;
	}



private:


	Status::statusType _SendSlaveAddress(uint8_t DevAddress) {
		uint32_t I2C_Time;

		// Устанавливаем бит ACK
		i2cHandle->CR1 |= I2C_CR1_ACK;

		// Устанавливаем бит START
		i2cHandle->CR1 |= I2C_CR1_START;

		// Ждём сброса SB
		I2C_Time = System::GetTick();
		while (!(i2cHandle->SR1 & I2C_SR1_SB)) {
			if (System::GetTick() > (I2C_Time + timeout)) {
				if (i2cHandle->CR1 & I2C_CR1_START) return Status::error;
			}
		}

		// Если адрес 7 бит пуляем его
		// Выплёвываем адрес со сброшенным битом RD
		i2cHandle->DR = (uint8_t) (DevAddress << 1) & ~1U;

		// Если 10бит оставляем место для пуляния
		// Ждём флага ADDR
		I2C_Time = System::GetTick();

		// Пока нет флагов, ждём
		while (((i2cHandle->SR1 & I2C_SR1_ADDR) == 0) && ((i2cHandle->SR1 & I2C_SR1_AF) == 0)) {
			if (System::GetTick() > (I2C_Time + timeout)) break;
		}

		// Если флаг ADDR всё таки был,
		if (i2cHandle->SR1 & I2C_SR1_ADDR) {
			// Посылаем STOP
			i2cHandle->CR1 |= I2C_CR1_STOP;

			// Чистим флаги
			(void) i2cHandle->SR1;
			(void) i2cHandle->SR2;

			// Если флаг BUSY установлен, а время вышло, это ошибка
			I2C_Time = System::GetTick();
			while (i2cHandle->SR2 & I2C_SR2_BUSY) {
				if (System::GetTick() > (I2C_Time + timeout)) return Status::timeout;
			}
			return Status::ok;
		} else {
			// Флага ADDR не было, посылаем STOP
			i2cHandle->CR1 |= I2C_CR1_STOP;

			// Чистим флаги
			(void) i2cHandle->SR1;
			(void) i2cHandle->SR2;

			// Если флаг BUSY установлен, а время вышло, это ошибка
			I2C_Time = System::GetTick();
			while (i2cHandle->SR2 & I2C_SR2_BUSY) {
				if (System::GetTick() > (I2C_Time + timeout)) return Status::timeout;
			}
			return Status::error;
		}
		return Status::ok;
	}





	Status::statusType _RequestDataRead(uint8_t DevAddress) {
		//  uint32_t  I2C_Time;
		Status::statusType Status;

		 // Включаем ACK
		i2cHandle->CR1 |= I2C_CR1_ACK;

		// Выдаём старт
		i2cHandle->CR1 |= I2C_CR1_START;

		// Проверяем SB и START
		if (_isSB_Flag(RESET)) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START) return Status::timeout;
		}

		// Загоняем адрес на линию в режиме записи
		// Выплёвываем адрес со сброшенным битом RD
		i2cHandle->DR = (uint8_t) (DevAddress << 1) & ~1U;

		// Проверяем флаг ADDR и AF
		Status = _isAddressAF_Flag();
		if (Status != Status::ok) return Status;

		// Очищаем флаги
		(void) i2cHandle->SR1;
		(void) i2cHandle->SR2;

		 // Выдаём старт
		i2cHandle->CR1 |= I2C_CR1_START;

		// Проверяем SB и START
		if (_isSB_Flag(RESET)) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START) return Status::timeout;
		}

		// Выплёвываем адрес с установленным битом RD
		i2cHandle->DR = (uint8_t) (DevAddress << 1) | 1U;

		// Проверяем флаг ADDR и AF
		Status = _isAddressAF_Flag();
		if (Status != Status::ok) return Status;
		//    }
		return Status::ok;
	}





	Status::statusType _RequestDataWrite(uint8_t DevAddress) {
		Status::statusType Status;

		// Запускаем START
		i2cHandle->CR1 |= I2C_CR1_START;

		if (_isSB_Flag(RESET) != I2C_OK) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START) {
				return Status::error;
			}
			return Status::timeout;
		}

		// Выдаём на шину адрес устройства
		i2cHandle->DR = (DevAddress << 1) & ~1U;

		// Проверяем флаг ADDR и AF
		Status = _isAddressAF_Flag();
		if (Status != Status::ok) return Status;

		return Status::ok;
	}





	Status::statusType _RequestMemoryRead(uint8_t DevAddress, uint16_t MemAddress, uint8_t MemAddSise) {
		//  uint32_t  I2C_Time;
		Status::statusType Status;

		// Включаем ACK
		i2cHandle->CR1 |= I2C_CR1_ACK;

		// Выдаём старт
		i2cHandle->CR1 |= I2C_CR1_START;

		// Проверяем SB и START
		if (_isSB_Flag(RESET)) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START) return Status::timeout;
		}

		// Загоняем адрес на линию в режиме записи
		// Выплёвываем адрес со сброшенным битом RD
		i2cHandle->DR = (uint8_t) (DevAddress << 1) & ~1U;

		// Проверяем флаг ADDR и AF
		Status = _isAddressAF_Flag();
		if (Status != Status::ok) return Status;

		// Очищаем флаги
		(void) i2cHandle->SR1;
		(void) i2cHandle->SR2;

		// Ждём когда поднимется TXE
		Status = _isTXE_Flag();
		if (Status != Status::ok) {
			if (Status == I2C_AF_ERROR) i2cHandle->CR1 |= I2C_CR1_STOP;
			return Status;
		}

		// Закидываем 8 или 16 бит адрес
		if (MemAddSise == 1) {
			// Пихаем 8бит адрес
			i2cHandle->DR = MemAddress & 0xFF;
		} else {

			// Пихаем старшие 8 бит адрес
			i2cHandle->DR = (MemAddress >> 8) & 0xFF;

			// Ждём когда поднимется TXE
			Status = _isTXE_Flag();
			if (Status != Status::ok) {
				if (Status == Status::error) i2cHandle->CR1 |= I2C_CR1_STOP;
				return Status;
			}
			// Пихаем младшие 8 бит
			i2cHandle->DR = MemAddress & 0xFF;

			// Ждём когда поднимется TXE
			Status = _isTXE_Flag();
			if (Status != Status::ok) {
				if (Status == Status::error) i2cHandle->CR1 |= I2C_CR1_STOP;
				return Status;
			}

			// Выдаём старт
			i2cHandle->CR1 |= I2C_CR1_START;

			// Проверяем SB и START
			if (_isSB_Flag(RESET)) {
				if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START) return Status::error;
			}

			// Выплёвываем адрес с установленным битом RD
			i2cHandle->DR = (uint8_t) (DevAddress << 1) | 1U;

			// Проверяем флаг ADDR и AF
			Status = _isAddressAF_Flag();
			if (Status != Status::ok) return Status;
		}
		return Status::ok;
	}





	Status::statusType _RequestMemoryWrite(uint8_t DevAddress, uint16_t MemAddress, uint8_t MemAddSise) {
		Status::statusType Status;

		// Запускаем START
		i2cHandle->CR1 |= I2C_CR1_START;

		if (_isSB_Flag(RESET) != Status::ok) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START) {
				return Status::error;
			}
			return Status::timeout;
		}

		// Выдаём на шину адрес устройства
		i2cHandle->DR = (DevAddress << 1) & ~1U;

		// Проверяем флаг ADDR и AF
		Status = _isAddressAF_Flag();
		if (Status != Status::ok) return Status;

		// Очищаем флаги
		(void) i2cHandle->SR1;
		(void) i2cHandle->SR2;

		// Ждём когда поднимется TXE
		Status = _isTXE_Flag();
		if (Status != Status::ok) {
			if (Status == Status::error) i2cHandle->CR1 |= I2C_CR1_STOP;
			return Status;
		}

		// Закидываем 8 или 16 бит адрес
		if (MemAddSise == 1) {
			// Пихаем 8бит адрес
			i2cHandle->DR = MemAddress & 0xFF;
		} else {
			// Пихаем старшие 8 бит адрес
			i2cHandle->DR = (MemAddress >> 8) & 0xFF;

			// Ждём когда поднимется TXE
			Status = _isTXE_Flag();
			if (Status != Status::ok) {
				if (Status == Status::error) i2cHandle->CR1 |= I2C_CR1_STOP;
				return Status;
			}
			// Пихаем младшие 8 бит
			i2cHandle->DR = MemAddress & 0xFF;

			// Ждём когда поднимется TXE
			Status = _isTXE_Flag();
			if (Status != Status::ok) {
				if (Status == Status::error) i2cHandle->CR1 |= I2C_CR1_STOP;
				return Status;
			}
		}
		return Status::ok;
	}





	Status::statusType _isBusyFlag(bool FlagSet) {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR2 & I2C_SR2_BUSY) >> I2C_SR2_BUSY_Pos) == FlagSet) {
			if ((System::GetTick() - TickStart) > timeout) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isSB_Flag(bool FlagSet) {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_SB) >> I2C_SR1_SB_Pos) == FlagSet) {
			if ((System::GetTick() - TickStart) > timeout) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isAddressAF_Flag() {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_ADDR) >> I2C_SR1_ADDR_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_AF) >> I2C_SR1_AF_Pos) == SET) {
				i2cHandle->CR1 |= I2C_CR1_STOP;
				i2cHandle->SR1 &= ~I2C_SR1_AF;
				return Status::error;
			}

			if ((System::GetTick() - TickStart) > timeout) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isTXE_Flag() {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_TXE) >> I2C_SR1_TXE_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_AF) >> I2C_SR1_AF_Pos) == SET) {
				i2cHandle->SR1 &= ~I2C_SR1_AF;
				return Status::error;
			}

			if ((System::GetTick() - TickStart) > timeout) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isRXNE_Flag() {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_RXNE) >> I2C_SR1_RXNE_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_STOPF) >> I2C_SR1_STOPF_Pos) == SET) {
				i2cHandle->SR1 &= ~I2C_SR1_STOPF;
				return Status::error;
			}

			if ((System::GetTick() - TickStart) > timeout) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isBTF_Flag(bool FlagSet) {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_BTF) >> I2C_SR1_BTF_Pos) == FlagSet) {
			if ((System::GetTick() - TickStart) > timeout) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}





	Status::statusType _isBTF_Flag_TimeOut() {
		uint32_t TickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_BTF) >> I2C_SR1_BTF_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_AF) >> I2C_SR1_AF_Pos) != I2C_OK)
				return Status::error;

			if ((System::GetTick() - TickStart) > timeout) {
				return Status::timeout;
			}
		}
		return Status::ok;
	}
};















