#pragma once
#include <Adapter/I2CAdapter.h>
#include <BSP.h>
#include <Utilities/Data/ByteConverter.h>


using ASI2C = class SoftwareI2C;


class SoftwareI2C : public I2CAdapter {
private:
	enum class Line { High, Low };

	AGPIO sdaPin;
	AGPIO sclPin;

	Status::statusType lastStatus = Status::ok;
	uint16 bitDelayUs = 100;
	bool clockStretchMode = true;
	uint32 timeoutMs = 1000;


public:
	SoftwareI2C() {}

	~SoftwareI2C() {
	    SetSda(Line::High);
	    SetScl(Line::High);
	}





	Status::statusType SetPins(AGPIO::IO scl, AGPIO::IO sda) {
	    sdaPin = AGPIO(sda);
	    sclPin = AGPIO(scl);

	    sdaPin.Reset().SetParameters({ AGPIO::Mode::OpenDrain });
	    sclPin.Reset().SetParameters({ AGPIO::Mode::OpenDrain });
	    DelayUs(bitDelayUs);

	    Release();

	    // TODO: Some tests could be added here, to check if the SDA and SCL are really turning high
	    return Status::ok;
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


	virtual Status::statusType WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *writeData, uint32 dataSize) override {
		Status::statusType status;

		status = Open();
		if(status != Status::ok) {
			Release();
			return status;
		}


		status = RequestWrite(device);
		if(status != Status::ok) {
			Release();
			return status;
		}


		status = Write(ByteConverter::GetByte(address, 0));
		if(status != Status::ok) {
			Release();
			return status;
		}


		if(addressSize != 1) {
			status = Write(ByteConverter::GetByte(address, 1));
			if(status != Status::ok) {
				Release();
				return status;
			}
		}


		for(uint32 i = 0; i < dataSize; i++) {
			status = Write((uint8)*writeData);
			writeData++;

			if(status != Status::ok) {
				Release();
				return status;
			}
		}


	    status = Close();
		if(status != Status::ok) {
			Release();
			return status;
		}


		return Status::ok;
	}





	virtual Status::statusType ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *readData, uint32 dataSize) override {
		Status::statusType status;

		status = Open();
		if(status != Status::ok) {
			Release();
			return status;
		}


		status = RequestWrite(device);
		if(status != Status::ok) {
			Release();
			return status;
		}


		status = Write(ByteConverter::GetByte(address, 0));
		if(status != Status::ok) {
			Release();
			return status;
		}


		if(addressSize != 1) {
			status = Write(ByteConverter::GetByte(address, 1));
			if(status != Status::ok) {
				Release();
				return status;
			}
		}


	    status = Close();
		if(status != Status::ok) {
			Release();
			return status;
		}


		// --------------------


		status = Open();
		if(status != Status::ok) {
			Release();
			return status;
		}


		status = RequestRead(device);
		if(status != Status::ok) {
			Release();
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
	        	Release();
	        	return readInfo.type;
	        }

	        *readData = readInfo.data;
	        readData++;
		}


	    status = Close();
		if(status != Status::ok) {
			Release();
			return status;
		}


		return Status::ok;
	}





	Status::statusType Open() {
	    return i2c_start();
	}




	// The stop was not recognized by every chip.
	// Some code has been added (with comment "ADDED1"),
	// to be sure that the levels are okay with delays in between.
	Status::statusType Close() {
		SetScl(Line::Low); // ADDED1, it should already be low.
		SetSda(Line::Low);

		// ADDED1, wait to be sure that the slave knows that both are low
		DelayUs(bitDelayUs);

		// For a stop, make SCL high wile SDA is still low
		SetScl(Line::High);

		if (clockStretchMode) {
		    auto prevMillis = System::GetTick();
		    while (ReadScl() == 0) {
		        if (System::GetTick() - prevMillis >= timeoutMs) {
		            break;
		        }
		    }
		}

		DelayUs(bitDelayUs);

		// complete the STOP by setting SDA high
		SetSda(Line::High);

		// It is not known how fast the next START will happen
		DelayUs(bitDelayUs);

	    return Status::ok;
	}





	void Release() {
	    SetScl(Line::High);
	    DelayUs(bitDelayUs);

	    SetSda(Line::High);
	    DelayUs(bitDelayUs * 4); // 4 times the normal delay, to claim the bus
	}





	// TODO: check if the repeated start actually works
	Status::statusType Restart() {
	    SetScl(Line::Low);
	    DelayUs(bitDelayUs);

	    SetSda(Line::High);
	    DelayUs(bitDelayUs);

	    SetScl(Line::High);
		DelayUs(bitDelayUs);

	    if (clockStretchMode) {
	        auto prevMillis = System::GetTick();
	        while (ReadScl() == 0) {
	            if (System::GetTick() - prevMillis >= timeoutMs) {
	                break;
	            }
	        }
	    }

		DelayUs(bitDelayUs);

	    return Status::ok;
	}





	Status::statusType RequestWrite(uint8 address) {
	    return i2c_write((address << 1) | 0);
	}





	Status::statusType RequestRead(uint8 address) {
	    return i2c_write((address << 1) | 1);
	}





	Status::info<uint8> Read() {
	    return { Status::ok, i2c_read(true) };
	}





	Status::info<uint8> ReadLast() {
	    return { Status::ok, i2c_read(false) };
	}





	Status::statusType Write(uint8 data) {
	    return i2c_write(data);
	}





	void SetFrequency(uint16 kHz) {
	    if (kHz > 1000) {
	        kHz = 1000; // 1MHz - max for soft I2C
	    }

	    bitDelayUs = 1000 / kHz;
	}





	void SetTimeout(uint32 ms) {
	    timeoutMs = ms;
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
	void i2c_writebit(uint8 c) {
	    if (c == 0) {
	        SetSda(Line::Low);
	    } else {
	        SetSda(Line::High);
	    }

	    DelayUs(bitDelayUs);

	    SetScl(Line::High);

	    if (clockStretchMode) {
	        auto prevMillis = System::GetTick();
	        while (ReadScl() == 0) {
	            if (System::GetTick() - prevMillis >= timeoutMs) {
	                break;
	            }
	        }
	    }

	    DelayUs(bitDelayUs);

	    SetScl(Line::Low);

	    DelayUs(bitDelayUs);
	}





	uint8 i2c_readbit() {
	    SetSda(Line::High);
	    SetScl(Line::High);

	    if (clockStretchMode) {
	        auto prevMillis = System::GetTick();
	        while (ReadScl() == 0) {
	            if (System::GetTick() - prevMillis >= timeoutMs) {
	                break;
	            }
	        }
	    }

	    DelayUs(bitDelayUs);

	    uint8 c = ReadSda();

	    SetScl(Line::Low);
	    DelayUs(bitDelayUs);

	    return c;
	}





	Status::statusType i2c_start() {

	    // TODO: can perhaps be removed some day ? if the rest of the code is okay
	    SetSda(Line::High);
	    SetScl(Line::High);

	    DelayUs(bitDelayUs);

	    // Both the sda and scl should be high.
	    // If not, there might be a hardware problem with the i2c bus signal lines.
	    // This check was added to prevent that a shortcut of sda would be seen as a valid ACK
	    // from a i2c Slave.
	    if (ReadSda() == 0 || ReadScl() == 0) {
	        return Status::error;
	    } else {
	        SetSda(Line::Low);
	        DelayUs(bitDelayUs);

	        SetScl(Line::Low);
	        DelayUs(bitDelayUs);
	    }

	    return Status::ok;
	}





	Status::statusType i2c_write(uint8 c) {
	    for (uint8 i = 0; i < 8; i++) {
	        i2c_writebit(c & 0x80); // highest bit first
	        c <<= 1;
	    }

	    return i2c_readbit() == 0 ? Status::ok : Status::error;
	}





	uint8 i2c_read(bool ack) {
	    uint8 res = 0;

	    for (uint8 i = 0; i < 8; i++) {
	        res <<= 1;
	        res |= i2c_readbit();
	    }

	    if (ack) {
	        i2c_writebit(0);
	    } else {
	        i2c_writebit(1);
	    }

	    DelayUs(bitDelayUs);

	    return res;
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





	void DelayUs(uint32 us) {
	    System::DelayUs(us);
	}
};

