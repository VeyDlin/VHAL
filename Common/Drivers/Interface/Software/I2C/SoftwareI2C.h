#pragma once
#include <Adapter/I2CAdapter.h>
#include <Utilities/Data/ByteConverter.h>


using ASI2C = class SoftwareI2C;


class SoftwareI2C : public I2CAdapter {
private:
	enum class Line:uint8 { High = 1, Low = 0 };

	AGPIO sdaPin;
	AGPIO sclPin;

	Status::statusType lastStatus = Status::ok;
	uint16 bitDelayUs = 100;
	bool clockStretchMode = true;


public:
	SoftwareI2C() {}
	SoftwareI2C(I2C_TypeDef *i2c, uint32 busClockHz):I2CAdapter(i2c, busClockHz) { }





	~SoftwareI2C() {
	    SetSda(Line::High);
	    SetScl(Line::High);
	}





	virtual void IrqEventHandler() override {}

	virtual void IrqErrorHandler() override {}

	virtual Status::statusType CheckDevice(uint8 deviceAddress, uint16 repeat = 1) override {
		return Status::notSupported;
	}

	virtual Status::statusType CheckDeviceAsync(uint8 deviceAddress, uint16 repeat = 1) override {
		return Status::notSupported;
	}

	virtual Status::info<uint8> Scan(uint8* listBuffer, uint8 size) override {
		return { Status::notSupported };
	}

	virtual Status::info<uint8> ScanAsync(uint8* listBuffer, uint8 size) override {
		return { Status::notSupported };
	}

	virtual Status::statusType WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return Status::notSupported;
	}

	virtual Status::statusType ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return Status::notSupported;
	}





	Status::statusType SetPins(AGPIO::IO scl, AGPIO::IO sda) {
	    sdaPin = AGPIO(sda);
	    sclPin = AGPIO(scl);

	    sdaPin.Reset().SetParameters({ AGPIO::Mode::OpenDrain });
	    sclPin.Reset().SetParameters({ AGPIO::Mode::OpenDrain });
	    Tick();

	    Release();

	    // TODO: Some tests could be added here, to check if the SDA and SCL are really turning high
	    return Status::ok;
	}





	virtual Status::statusType WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *writeData, uint32 dataSize) override {
		Status::statusType status;

		status = Open();
		if(status != Status::ok) {
			Close();
			return status;
		}


		status = RequestWrite(device);
		if(status != Status::ok) {
			Close();
			return status;
		}


		status = Write(ByteConverter::GetByte(address, 0));
		if(status != Status::ok) {
			Close();
			return status;
		}


		if(addressSize != 1) {
			status = Write(ByteConverter::GetByte(address, 1));
			if(status != Status::ok) {
				Close();
				return status;
			}
		}


		for(uint32 i = 0; i < dataSize; i++) {
			status = Write((uint8)*writeData);
			writeData++;

			if(status != Status::ok) {
				Close();
				return status;
			}
		}


	    status = Close();
		if(status != Status::ok) {
			return status;
		}


		return Status::ok;
	}





	virtual Status::statusType ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *readData, uint32 dataSize) override {
		Status::statusType status;

		status = Open();
		if(status != Status::ok) {
			Close();
			return status;
		}


		status = RequestWrite(device);
		if(status != Status::ok) {
			Close();
			return status;
		}


		status = Write(ByteConverter::GetByte(address, 0));
		if(status != Status::ok) {
			Close();
			return status;
		}


		if(addressSize != 1) {
			status = Write(ByteConverter::GetByte(address, 1));
			if(status != Status::ok) {
				Close();
				return status;
			}
		}


	    status = Close();
		if(status != Status::ok) {
			Close();
			return status;
		}


		// --------------------


		status = Open();
		if(status != Status::ok) {
			Close();
			return status;
		}


		status = RequestRead(device);
		if(status != Status::ok) {
			Close();
			return status;
		}


		for(uint32 i = 0; i < dataSize; i++) {
			Status::info<uint8> readInfo;

	        if(i < dataSize - 1) {
	        	readInfo = Read();
	        } else {
	        	readInfo = ReadLast();
	        }

	        if(readInfo.IsError()) {
	        	Close();
	        	return readInfo.type;
	        }

	        *readData = readInfo.data;
	        readData++;
		}


	    status = Close();
		if(status != Status::ok) {
			return status;
		}


		return Status::ok;
	}





	Status::statusType Open() {
	    return LLStart();
	}


	Status::statusType Close() {
		return LLStop();
	}


	Status::statusType Restart() {
	    return LLRestart();
	}


	Status::statusType RequestWrite(uint8 address) {
	    return LLWrite((address << 1) | 0);
	}


	Status::statusType RequestRead(uint8 address) {
	    return LLWrite((address << 1) | 1);
	}

	Status::info<uint8> Read() {
	    return LLRead(true);
	}

	Status::info<uint8> ReadLast() {
	    return LLRead(false);
	}


	Status::statusType Write(uint8 data) {
	    return LLWrite(data);
	}


	Status::statusType Free() {
		return Release();
	}


	void SetFrequency(uint16 kHz) {
	    if (kHz > 1000) {
	        kHz = 1000; // 1MHz - max for soft I2C
	    }

	    bitDelayUs = 1000 / kHz;
	}





protected:
	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		SetFrequency(static_cast<uint16>(parameters.speed));

		return AfterInitialization();
	}





private:
	Status::statusType LLStart() {

	    // TODO: can perhaps be removed some day ? if the rest of the code is okay
	    SetSda(Line::High);
	    SetScl(Line::High);
	    Tick();


	    if (ReadSda() == 0 || ReadScl() == 0) {
	        return Status::error;
	    }

		// Force SDA low
		SetSda(Line::Low);
		Tick();

		// Force SCL low
		SetScl(Line::Low);
		Tick();


	    if (ReadSda() != 0 || ReadScl() != 0) {
	        return Status::busy;
	    }


	    return Status::ok;
	}





	// TODO: check if the repeated start actually works
	Status::statusType LLRestart() {

		// Force SCL low
		SetScl(Line::Low);
		Tick();

		// Release SDA
		SetSda(Line::High);
		Tick();

		// Release SCL
		SetScl(Line::High);
		if (clockStretchMode) {
			if (WaitScl(Line::High)) {
				return Status::timeout;
			}
		}
		Tick();

		// Force SDA low
		SetSda(Line::Low);
		Tick();

		return Status::ok;
	}





	Status::statusType LLStop() {

		// Force SCL low
		SetScl(Line::Low);
		Tick();

		// Force SDA low
		SetSda(Line::Low);
		Tick();

		return Release();
	}





	Status::statusType LLWrite(uint8 c) {
	    for (uint8 i = 0; i < 8; i++) {

	    	auto wrriteStatus = LLWriteBit(c & 0x80);
	    	if(wrriteStatus != Status::ok) {
	    		return wrriteStatus;
	    	}

	        c <<= 1;
	    }

        auto bitInfo = LLReadBit();
        if(bitInfo.IsError()) {
        	return bitInfo.type;
        }

	    return bitInfo.data == 0 ? Status::ok : Status::error;
	}





	Status::info<uint8> LLRead(bool ack) {
	    uint8 res = 0;

	    for (uint8 i = 0; i < 8; i++) {
	        res <<= 1;

	        auto bitInfo = LLReadBit();
	        if(bitInfo.IsError()) {
	        	return bitInfo;
	        }

	        res |= bitInfo.data;
	    }

    	auto wrriteStatus = LLWriteBit(ack ? 0 : 1);
    	if(wrriteStatus != Status::ok) {
    		return { wrriteStatus };
    	}

	    Tick();

	    return { Status::ok, res };
	}





	Status::statusType LLWriteBit(uint8 c) {
	    SetSda(c == 0 ? Line::Low : Line::High);
	    Tick();

	    SetScl(Line::High);

	    if (clockStretchMode) {
			if (WaitScl(Line::High)) {
				return Status::timeout;
			}
	    }

	    Tick();

	    SetScl(Line::Low);
	    Tick();

	    return Status::ok;
	}





	Status::info<uint8> LLReadBit() {
	    SetSda(Line::High);
	    SetScl(Line::High);

	    if (clockStretchMode) {
			if (WaitScl(Line::High)) {
				return { Status::timeout };
			}
	    }

	    Tick();

	    uint8 bit = ReadSda();

	    SetScl(Line::Low);
	    Tick();

	    return { Status::ok, bit };
	}





	Status::statusType Release() {
	    SetScl(Line::High);
	    Tick();

	    SetSda(Line::High);
	    Tick(10); // 10 times the normal delay, to claim the bus


		auto prevMillis = System::GetTick();
		while (ReadScl() != 1 && ReadSda() != 1) {
			if (System::GetTick() - prevMillis >= timeout) {
				return Status::timeout;
			}
		}

		return Status::ok;
	}





	void SetScl(Line state) {
	    sclPin.SetState(state != Line::Low);
	}


	void SetSda(Line state) {
	    sdaPin.SetState(state != Line::Low);
	}


	uint8 ReadScl() {
	    return static_cast<uint8>(sclPin.GetState());
	}


	uint8 ReadSda() {
	    return static_cast<uint8>(sdaPin.GetState());
	}






	Status::statusType WaitScl(Line waitState) {
		auto prevMillis = System::GetTick();
		while (ReadScl() != static_cast<uint8>(waitState)) {
			if (System::GetTick() - prevMillis >= timeout) {
				return Status::timeout;
			}
		}

		return Status::ok;
	}





	void Tick(uint8 ticks = 1) {
		if (ticks != 0) {
			System::DelayUs(bitDelayUs * ticks);
		}
	}
};

