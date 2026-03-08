#pragma once
#include <Adapter/I2CAdapter.h>
#include <Utilities/Data/ByteConverter.h>


using ASI2C = class SoftwareI2C;

class SoftwareI2C : public I2CAdapter<void> {
private:
	enum class Line:uint8 { High = 1, Low = 0 };

	AGPIO sdaPin;
	AGPIO sclPin;

	ResultStatus lastStatus = ResultStatus::ok;
	uint16 bitDelayUs = 100;
	bool clockStretchMode = true;


public:
	SoftwareI2C() {}
	SoftwareI2C(void *i2c, uint32 busClockHz):I2CAdapter(i2c, busClockHz) { }


	~SoftwareI2C() {
	    SetSda(Line::High);
	    SetScl(Line::High);
	}


	virtual void IrqEventHandler() override {}

	virtual void IrqErrorHandler() override {}

	virtual ResultStatus CheckDevice(uint8 deviceAddress, uint16 repeat = 1) override {
		return ResultStatus::notSupported;
	}

	virtual ResultStatus CheckDeviceAsync(uint8 deviceAddress, uint16 repeat = 1) override {
		return ResultStatus::notSupported;
	}

	virtual Result<uint8> Scan(uint8* listBuffer, uint8 size) override {
		return { ResultStatus::notSupported };
	}

	virtual Result<uint8> ScanAsync(uint8* listBuffer, uint8 size) override {
		return { ResultStatus::notSupported };
	}

	virtual ResultStatus WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return ResultStatus::notSupported;
	}

	virtual ResultStatus ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return ResultStatus::notSupported;
	}


	ResultStatus SetPins(AGPIO::IO scl, AGPIO::IO sda) {
	    sdaPin = AGPIO(sda);
	    sclPin = AGPIO(scl);

	    sdaPin.Reset().SetParameters({ AGPIO::Mode::OpenDrain });
	    sclPin.Reset().SetParameters({ AGPIO::Mode::OpenDrain });
	    Tick();

	    Release();

	    // TODO: Some tests could be added here, to check if the SDA and SCL are really turning high
	    return ResultStatus::ok;
	}


	virtual ResultStatus WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *writeData, uint32 dataSize) override {
		ResultStatus status;

		status = Open();
		if(status != ResultStatus::ok) {
			Close();
			return status;
		}

		status = RequestWrite(device);
		if(status != ResultStatus::ok) {
			Close();
			return status;
		}

		status = Write(ByteConverter::GetByte(address, 0));
		if(status != ResultStatus::ok) {
			Close();
			return status;
		}

		if(addressSize != 1) {
			status = Write(ByteConverter::GetByte(address, 1));
			if(status != ResultStatus::ok) {
				Close();
				return status;
			}
		}

		for(uint32 i = 0; i < dataSize; i++) {
			status = Write((uint8)*writeData);
			writeData++;

			if(status != ResultStatus::ok) {
				Close();
				return status;
			}
		}

	    status = Close();
		if(status != ResultStatus::ok) {
			return status;
		}

		return ResultStatus::ok;
	}


	virtual ResultStatus ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *readData, uint32 dataSize) override {
		ResultStatus status;

		status = Open();
		if(status != ResultStatus::ok) {
			Close();
			return status;
		}


		status = RequestWrite(device);
		if(status != ResultStatus::ok) {
			Close();
			return status;
		}


		status = Write(ByteConverter::GetByte(address, 0));
		if(status != ResultStatus::ok) {
			Close();
			return status;
		}


		if(addressSize != 1) {
			status = Write(ByteConverter::GetByte(address, 1));
			if(status != ResultStatus::ok) {
				Close();
				return status;
			}
		}


	    status = Close();
		if(status != ResultStatus::ok) {
			Close();
			return status;
		}

		// --------------------

		status = Open();
		if(status != ResultStatus::ok) {
			Close();
			return status;
		}

		status = RequestRead(device);
		if(status != ResultStatus::ok) {
			Close();
			return status;
		}

		for(uint32 i = 0; i < dataSize; i++) {
			Result<uint8> readInfo;

	        if(i < dataSize - 1) {
	        	readInfo = Read();
	        } else {
	        	readInfo = ReadLast();
	        }

	        if(readInfo.IsErr()) {
	        	Close();
	        	return readInfo.Error();
	        }

	        *readData = readInfo.Value();
	        readData++;
		}

	    status = Close();
		if(status != ResultStatus::ok) {
			return status;
		}

		return ResultStatus::ok;
	}


	ResultStatus Open() {
	    return LLStart();
	}


	ResultStatus Close() {
		return LLStop();
	}


	ResultStatus Restart() {
	    return LLRestart();
	}


	ResultStatus RequestWrite(uint8 address) {
	    return LLWrite((address << 1) | 0);
	}


	ResultStatus RequestRead(uint8 address) {
	    return LLWrite((address << 1) | 1);
	}

	Result<uint8> Read() {
	    return LLRead(true);
	}

	Result<uint8> ReadLast() {
	    return LLRead(false);
	}


	ResultStatus Write(uint8 data) {
	    return LLWrite(data);
	}


	ResultStatus Free() {
		return Release();
	}


	void SetFrequency(uint16 kHz) {
	    if (kHz > 1000) {
	        kHz = 1000; // 1MHz - max for soft I2C
	    }

	    bitDelayUs = 1000 / kHz;
	}


protected:
	virtual ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		SetFrequency(static_cast<uint16>(parameters.speed));

		return AfterInitialization();
	}


private:
	ResultStatus LLStart() {

	    // TODO: can perhaps be removed some day ? if the rest of the code is okay
	    SetSda(Line::High);
	    SetScl(Line::High);
	    Tick();


	    if (ReadSda() == 0 || ReadScl() == 0) {
	        return ResultStatus::error;
	    }

		// Force SDA low
		SetSda(Line::Low);
		Tick();

		// Force SCL low
		SetScl(Line::Low);
		Tick();


	    if (ReadSda() != 0 || ReadScl() != 0) {
	        return ResultStatus::busy;
	    }


	    return ResultStatus::ok;
	}


	// TODO: check if the repeated start actually works
	ResultStatus LLRestart() {

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
				return ResultStatus::timeout;
			}
		}
		Tick();

		// Force SDA low
		SetSda(Line::Low);
		Tick();

		return ResultStatus::ok;
	}


	ResultStatus LLStop() {

		// Force SCL low
		SetScl(Line::Low);
		Tick();

		// Force SDA low
		SetSda(Line::Low);
		Tick();

		return Release();
	}


	ResultStatus LLWrite(uint8 c) {
	    for (uint8 i = 0; i < 8; i++) {

	    	auto wrriteStatus = LLWriteBit(c & 0x80);
	    	if(wrriteStatus != ResultStatus::ok) {
	    		return wrriteStatus;
	    	}

	        c <<= 1;
	    }

        auto bitInfo = LLReadBit();
        if(bitInfo.IsErr()) {
        	return bitInfo.Error();
        }

	    return bitInfo.Value() == 0 ? ResultStatus::ok : ResultStatus::error;
	}


	Result<uint8> LLRead(bool ack) {
	    uint8 res = 0;

	    for (uint8 i = 0; i < 8; i++) {
	        res <<= 1;

	        auto bitInfo = LLReadBit();
	        if(bitInfo.IsErr()) {
	        	return bitInfo;
	        }

	        res |= bitInfo.Value();
	    }

    	auto wrriteStatus = LLWriteBit(ack ? 0 : 1);
    	if(wrriteStatus != ResultStatus::ok) {
    		return { wrriteStatus };
    	}

	    Tick();

	    return { ResultStatus::ok, res };
	}


	ResultStatus LLWriteBit(uint8 c) {
	    SetSda(c == 0 ? Line::Low : Line::High);
	    Tick();

	    SetScl(Line::High);

	    if (clockStretchMode) {
			if (WaitScl(Line::High)) {
				return ResultStatus::timeout;
			}
	    }

	    Tick();

	    SetScl(Line::Low);
	    Tick();

	    return ResultStatus::ok;
	}


	Result<uint8> LLReadBit() {
	    SetSda(Line::High);
	    SetScl(Line::High);

	    if (clockStretchMode) {
			if (WaitScl(Line::High)) {
				return { ResultStatus::timeout };
			}
	    }

	    Tick();

	    uint8 bit = ReadSda();

	    SetScl(Line::Low);
	    Tick();

	    return { ResultStatus::ok, bit };
	}


	ResultStatus Release() {
	    SetScl(Line::High);
	    Tick();

	    SetSda(Line::High);
	    Tick(10); // 10 times the normal delay, to claim the bus


		auto prevMillis = System::GetTick();
		while (ReadScl() != 1 && ReadSda() != 1) {
			if (System::GetTick() - prevMillis >= timeout) {
				return ResultStatus::timeout;
			}
		}

		return ResultStatus::ok;
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


	ResultStatus WaitScl(Line waitState) {
		auto prevMillis = System::GetTick();
		while (ReadScl() != static_cast<uint8>(waitState)) {
			if (System::GetTick() - prevMillis >= timeout) {
				return ResultStatus::timeout;
			}
		}

		return ResultStatus::ok;
	}


	void Tick(uint8 ticks = 1) {
		if (ticks != 0) {
			System::DelayUs(bitDelayUs * ticks);
		}
	}
};

