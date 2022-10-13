#pragma once
#include <Adapter/I2CAdapter.h>
#include <BSP.h>
#include <Utilities/Data/ByteConverter.h>


using ASI2C = class SoftwareI2C;


class SoftwareI2C : public I2CAdapter {
private:
	enum class Line:uint8 { High = 1, Low = 0 };

	AGPIO sdaPin;
	AGPIO sclPin;

	uint16 bitDelayUs = 100;
	bool parameters.clockStretching = true;


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
	    Tick();

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
		return Status::notSupported;
	}

	virtual Status::info<uint8> ScanAsync(uint8* listBuffer, uint8 size) override {
		return Status::notSupported;
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





	Status::statusType Close() {

		// Force SCL low
		SetScl(Line::Low);
		Tick();

		// Force SDA low
		SetSda(Line::Low);
		Tick();

		// Release SCL
		SetScl(Line::High);
		if (parameters.clockStretching) {
			WaitScl(Line::High);
		}
		Tick();

		// Release SDA
		SetSda(Line::High);
		Tick();

	    return Status::ok;
	}





	void Release() {
	    SetScl(Line::High);
	    Tick();

	    SetSda(Line::High);
		Tick(4); // 4 times the normal delay, to claim the bus
	}





	// TODO: check if the repeated start actually works
	Status::statusType Restart() {

		// Force SCL low
		SetScl(Line::Low);
		Tick();

		// Release SDA
		SetSda(Line::High);
		Tick();

		// Release SCL
		SetScl(Line::High);
		if (parameters.clockStretching) {
			WaitScl(Line::High);
		}
		Tick();

		// Force SDA low
		SetSda(Line::Low);
		Tick();

	    return Status::ok;
	}





	Status::statusType RequestWrite(uint8 address) {
	    return i2c_write((address << 1) | 0);
	}





	Status::statusType RequestRead(uint8 address) {
	    return i2c_write((address << 1) | 1);
	}





	Status::info<uint8> Read() {
	    return i2c_read(true);
	}





	Status::info<uint8> ReadLast() {
	    return i2c_read(false);
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
	Status::statusType i2c_start() {

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





	Status::statusType i2c_write(uint8 data) {
		auto prevMillis = System::GetTick();

		for (uint8 i = 8; i; --i) {
			// Force SCL low
			SetScl(Line::Low);

			if (data & 0x80) {
				// Release SDA
				SetSda(Line::High);
			} else {
				// Force SDA low
				SetScl(Line::Low);
			}
			Tick();

			// Release SCL
			SetScl(Line::High);
			if (parameters.clockStretching) {
				WaitScl(Line::High);
			}
			Tick();

			data <<= 1;
			if (System::GetTick() - prevMillis >= timeout) {
				stop(); // Reset bus
				return Status::timeout;
			}
		}

		// Get ACK
		// Force SCL low
		SetScl(Line::Low);

		// Release SDA
		SetSda(Line::High);
		Tick();

		// Release SCL
		SetScl(Line::High);
		if (parameters.clockStretching) {
			WaitScl(Line::High);
		}

		auto state = ReadSda() == 0 ? Status::ok : Status::error

		Tick();

		// Keep SCL low between bytes
		SetScl(Line::Low);

	    return state;
	}





	Status::info<uint8> i2c_read(bool sendAck) {
		Status::info<uint8> dataInfo;
		dataInfo.data = 0;
	
		for (uint8 i = 8; i; --i) {
			dataInfo.data <<= 1;

			// Force SCL low
			SetScl(Line::Low);

			// Release SDA (from previous ACK)
			SetSda(Line::High);
			Tick();

			// Release SCL
			SetScl(Line::High);
			if (parameters.clockStretching) {
				WaitScl(Line::High);
			}
			Tick();

			// Read clock stretch
			if (WaitScl(Line::High) != Status::ok) {
				Close(); // Reset bus
				dataInfo.type = Status::timeout;
				return dataInfo;
			}

			if (ReadSda()) {
				dataInfo.data |= 1;
			}
		}

		// Put ACK/NACK
		// Force SCL low
		SetScl(Line::Low);

		// Force SDA low / Release SDA
		SetSda(sendAck ? Line::Low : Line::High);

		Tick();

		// Release SCL
		SetScl(Line::High);
		if (parameters.clockStretching) {
			WaitScl(Line::High);
		}
		Tick();

		// Wait for SCL to return high
		if (WaitScl(Line::High) != Status::ok) {
			Close(); // Reset bus
			dataInfo.type = Status::timeout;
			return dataInfo;
		}
		Tick();

		// Keep SCL low between bytes
		SetScl(Line::Low);

	    return dataInfo;
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

