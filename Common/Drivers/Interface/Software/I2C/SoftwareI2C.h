#pragma once
#include <Adapter/I2CAdapter.h>


using ASI2C = class SoftwareI2C;


class SoftwareI2C : public I2CAdapter<void> {
private:
	enum class Line:uint8 { High = 1, Low = 0 };


	static constexpr uint8 maxDeviceAddress = 0x7F;
	static constexpr uint8 maxRegisterAddressSize = 2;
	static constexpr uint32 maxByteAddress = 0xFF;
	static constexpr uint8 recoveryClocks = 9;
	static constexpr uint16 maxFrequencyKHz = 400;
	static constexpr uint32 standardModePeriodUs = 10;
	static constexpr uint8 firstScanAddress = 0x08;
	static constexpr uint8 lastScanAddress = 0x77;

	AGPIO sdaPin;
	AGPIO sclPin;
	bool pinsAttached = false;
	bool pinsReady = false;

	uint32 bitPeriodUs = 10;
	uint32 transferTimeoutMs = 1000;
	uint64 transferDeadlineMs = 0;


public:
	SoftwareI2C() = default;

	SoftwareI2C(void *i2c, uint32 busClockHz):I2CAdapter(i2c, busClockHz) { }


	~SoftwareI2C() {
		if (pinsAttached) {
			SetSda(Line::High);
			SetScl(Line::High);
		}
	}


	virtual void IrqEventHandler() override {}

	virtual void IrqErrorHandler() override {}


	virtual ResultStatus CheckDevice(uint8 device, uint16 repeat = 1) override {
		if (repeat == 0) {
			return ResultStatus::invalidParameter;
		}

		ResultStatus status = CheckTransfer(device, 0, 0);
		if (status != ResultStatus::ok) {
			return status;
		}

		for (uint16 attempt = 0; attempt < repeat; attempt++) {
			status = ProbeDevice(device);
			if (status != ResultStatus::nack) {
				return status;
			}
		}

		return status;
	}


	virtual ResultStatus CheckDeviceAsync(uint8, uint16 = 1) override {
		return ResultStatus::notSupported;
	}


	virtual Result<uint8> Scan(uint8 *listBuffer, uint8 size) override {
		uint8 count = 0;

		for (uint8 device = firstScanAddress; device <= lastScanAddress && count < size; device++) {
			ResultStatus status = CheckDevice(device, 1);
			if (status == ResultStatus::ok) {
				listBuffer[count] = device;
				count++;
			} else if (status != ResultStatus::nack) {
				return status;
			}
		}

		return count;
	}


	virtual Result<uint8> ScanAsync(uint8 *, uint8) override {
		return ResultStatus::notSupported;
	}


	// Sets both lines to open-drain and releases them. A failed pin setup is returned. A held SDA gets nine clocks and a STOP,
	// then busy if it stays low. A held SCL is busy at once.
	ResultStatus SetPins(AGPIO::IO scl, AGPIO::IO sda) {
		sdaPin = AGPIO(sda);
		sclPin = AGPIO(scl);
		pinsAttached = true;
		pinsReady = false;

		sdaPin.SetState(true);
		ResultStatus status = sdaPin.SetParameters({ AGPIO::Mode::OpenDrain });
		if (status != ResultStatus::ok) {
			return status;
		}

		sclPin.SetState(true);
		status = sclPin.SetParameters({ AGPIO::Mode::OpenDrain });
		if (status != ResultStatus::ok) {
			return status;
		}

		StartDeadline();
		status = ReleaseBus();
		if (status != ResultStatus::ok) {
			return status;
		}

		pinsReady = true;
		return ResultStatus::ok;
	}


	virtual ResultStatus WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *writeData, uint32 dataSize) override {
		ResultStatus status = CheckTransfer(device, address, addressSize);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = LLStart();
		if (status != ResultStatus::ok) {
			return status;
		}

		return FinishTransfer(WriteBody(device, address, addressSize, writeData, dataSize));
	}


	virtual ResultStatus ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *readData, uint32 dataSize) override {
		ResultStatus status = CheckTransfer(device, address, addressSize);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = LLStart();
		if (status != ResultStatus::ok) {
			return status;
		}

		return FinishTransfer(ReadBody(device, address, addressSize, readData, dataSize));
	}


	virtual ResultStatus WriteByteArrayAsync(uint8, uint16, uint8, uint8*, uint32) override {
		return ResultStatus::notSupported;
	}


	virtual ResultStatus ReadByteArrayAsync(uint8, uint16, uint8, uint8*, uint32) override {
		return ResultStatus::notSupported;
	}


	// Accepts only the settings the bit-banged driver can apply: the Low, Standard and Fast speeds, and the defaults of every
	// other field. Any other value returns notSupported and leaves the current settings unchanged.
	virtual ResultStatus SetParameters(Parameters value) override {
		if (!IsSupported(value)) {
			return ResultStatus::notSupported;
		}

		return I2CAdapter<void>::SetParameters(value);
	}


	// The longest a single transfer may take, including every clock stretch. Must be above zero.
	ResultStatus SetTransferTimeout(uint32 time) {
		if (time == 0) {
			return ResultStatus::invalidParameter;
		}

		transferTimeoutMs = time;
		return ResultStatus::ok;
	}


	// Sends START. Close sends the STOP. The total transfer timeout starts at Open, Restart, RequestWrite and RequestRead.
	ResultStatus Open() {
		return LLStart();
	}


	ResultStatus Close() {
		if (!pinsReady) {
			return ResultStatus::noInit;
		}

		return LLStop();
	}


	ResultStatus Restart() {
		if (!pinsReady) {
			return ResultStatus::noInit;
		}

		StartDeadline();
		return LLRestart();
	}


	ResultStatus RequestWrite(uint8 device) {
		if (!pinsReady) {
			return ResultStatus::noInit;
		}

		if (device > maxDeviceAddress) {
			return ResultStatus::invalidParameter;
		}

		StartDeadline();
		return SendAddress(device, false);
	}


	ResultStatus RequestRead(uint8 device) {
		if (!pinsReady) {
			return ResultStatus::noInit;
		}

		if (device > maxDeviceAddress) {
			return ResultStatus::invalidParameter;
		}

		StartDeadline();
		return SendAddress(device, true);
	}


	Result<uint8> Read() {
		if (!pinsReady) {
			return ResultStatus::noInit;
		}

		uint8 value = 0;
		ResultStatus status = ReadByte(value, true);
		return Result<uint8>::Capture(status, value);
	}


	Result<uint8> ReadLast() {
		if (!pinsReady) {
			return ResultStatus::noInit;
		}

		uint8 value = 0;
		ResultStatus status = ReadByte(value, false);
		return Result<uint8>::Capture(status, value);
	}


	using I2CAdapter<void>::Read;


	ResultStatus Write(uint8 data) {
		if (!pinsReady) {
			return ResultStatus::noInit;
		}

		return WriteByte(data);
	}


	using I2CAdapter<void>::Write;


	// Releases both lines and reports busy when one of them stays low.
	ResultStatus Free() {
		if (!pinsReady) {
			return ResultStatus::noInit;
		}

		StartDeadline();
		SetSda(Line::High);
		SetScl(Line::High);
		return WaitBusFree();
	}


	// Sets the bit rate in kHz, from 1 to 400 (Fast mode). A bit lasts whole microseconds, rounded up, so the bus never runs
	// faster than requested. A bit of one microsecond would leave no low half that the I2C minimum allows, so 1000 kHz is refused.
	ResultStatus SetFrequency(uint16 kHz) {
		if (kHz == 0 || kHz > maxFrequencyKHz) {
			return ResultStatus::invalidParameter;
		}

		bitPeriodUs = static_cast<uint32>((1000u + kHz - 1u) / kHz);
		return ResultStatus::ok;
	}


protected:
	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		status = SetFrequency(static_cast<uint16>(parameters.speed));
		if (status != ResultStatus::ok) {
			return status;
		}

		return AfterInitialization();
	}


private:
	static bool IsSupported(const Parameters &value) {
		Parameters defaults;

		return value.mode == Mode::Master
			&& value.addressingMode == AddressingMode::B7
			&& value.clockStretching
			&& (value.speed == Speed::Low || value.speed == Speed::Standard || value.speed == Speed::Fast)
			&& value.slaveAddress == defaults.slaveAddress
			&& value.dutyCycle == defaults.dutyCycle
			&& value.analogFilter == defaults.analogFilter
			&& value.digitalFilter == defaults.digitalFilter
			&& value.riseTimeNs == defaults.riseTimeNs
			&& value.fallTimeNs == defaults.fallTimeNs;
	}


	// Checks the arguments of a transfer. Nothing is sent when this fails.
	ResultStatus CheckTransfer(uint8 device, uint16 address, uint8 addressSize) {
		if (!pinsReady) {
			return ResultStatus::noInit;
		}

		if (device > maxDeviceAddress) {
			return ResultStatus::invalidParameter;
		}

		if (addressSize > maxRegisterAddressSize) {
			return ResultStatus::invalidParameter;
		}

		if (addressSize == 0 && address != 0) {
			return ResultStatus::invalidParameter;
		}

		if (addressSize == 1 && address > maxByteAddress) {
			return ResultStatus::invalidParameter;
		}

		return ResultStatus::ok;
	}


	ResultStatus WriteBody(uint8 device, uint16 address, uint8 addressSize, uint8 *writeData, uint32 dataSize) {
		ResultStatus status = SendAddress(device, false);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = WriteRegisterAddress(address, addressSize);
		if (status != ResultStatus::ok) {
			return status;
		}

		for (uint32 index = 0; index < dataSize; index++) {
			status = WriteByte(writeData[index]);
			if (status != ResultStatus::ok) {
				return status;
			}
		}

		return ResultStatus::ok;
	}


	// With a register address, the read is a write of the address, a repeated START, then the read. Without one, the read
	// follows the START directly.
	ResultStatus ReadBody(uint8 device, uint16 address, uint8 addressSize, uint8 *readData, uint32 dataSize) {
		ResultStatus status = ResultStatus::ok;

		if (addressSize > 0) {
			status = SendAddress(device, false);
			if (status != ResultStatus::ok) {
				return status;
			}

			status = WriteRegisterAddress(address, addressSize);
			if (status != ResultStatus::ok) {
				return status;
			}

			status = LLRestart();
			if (status != ResultStatus::ok) {
				return status;
			}
		}

		status = SendAddress(device, true);
		if (status != ResultStatus::ok) {
			return status;
		}

		for (uint32 index = 0; index < dataSize; index++) {
			status = ReadByte(readData[index], index + 1 < dataSize);
			if (status != ResultStatus::ok) {
				return status;
			}
		}

		return ResultStatus::ok;
	}


	// The register address goes out most significant byte first, as the hardware adapters send it.
	ResultStatus WriteRegisterAddress(uint16 address, uint8 addressSize) {
		for (uint8 index = addressSize; index > 0; index--) {
			uint8 shift = static_cast<uint8>((index - 1) * 8);
			ResultStatus status = WriteByte(static_cast<uint8>(address >> shift));
			if (status != ResultStatus::ok) {
				return status;
			}
		}

		return ResultStatus::ok;
	}


	ResultStatus SendAddress(uint8 device, bool read) {
		return WriteByte(static_cast<uint8>((device << 1) | (read ? 1 : 0)));
	}


	// A probe is not a transfer error, so it does not call onError on a NACK.
	ResultStatus ProbeDevice(uint8 device) {
		ResultStatus status = LLStart();
		if (status != ResultStatus::ok) {
			return status;
		}

		return StopAfter(SendAddress(device, false));
	}


	ResultStatus FinishTransfer(ResultStatus status) {
		if (status == ResultStatus::nack) {
			CallError(Error::AcknowledgeFailure);
		}

		return StopAfter(status);
	}


	// Sends STOP after a transfer. The first error of the transfer is returned, or the result of the STOP.
	ResultStatus StopAfter(ResultStatus status) {
		ResultStatus stop = LLStop();
		if (status != ResultStatus::ok) {
			return status;
		}

		return stop;
	}


	void StartDeadline() {
		transferDeadlineMs = System::GetTick() + transferTimeoutMs;
	}


	bool IsWaitExpired(uint64 start) {
		uint64 now = System::GetTick();
		return now - start >= timeout || now >= transferDeadlineMs;
	}


	bool IsTransferExpired() {
		return System::GetTick() >= transferDeadlineMs;
	}


	// Starts a transfer. Its deadline is set here, so every wait inside it is bounded by the total.
	ResultStatus LLStart() {
		if (!pinsReady) {
			return ResultStatus::noInit;
		}

		StartDeadline();
		ResultStatus status = ReleaseBus();
		if (status != ResultStatus::ok) {
			return status;
		}

		HoldHigh();
		return SendStart();
	}


	// A repeated START, between the register address and the read. SDA is released while SCL is low, and must read high once SCL
	// is high. A held SDA is busy and nothing is sent. The check reads the line before SendStart pulls it low, because after that
	// the driver would only read back its own drive.
	ResultStatus LLRestart() {
		SetScl(Line::Low);
		SetSda(Line::High);
		ResultStatus status = ClockHigh();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (ReadSda() == 0) {
			SetScl(Line::Low);
			return ResultStatus::busy;
		}

		return SendStart();
	}


	// Sends STOP. The bus is free when both lines are high again. Pins must be set, and the callers check that.
	ResultStatus LLStop() {
		SetScl(Line::Low);
		HoldLow();
		SetSda(Line::Low);
		HoldLow();
		SetScl(Line::High);

		ResultStatus status = WaitScl(Line::High);
		if (status != ResultStatus::ok) {
			// SCL is low here, so releasing SDA cannot make a START or a STOP.
			SetSda(Line::High);
			return status;
		}

		HoldHigh();
		SetSda(Line::High);
		return WaitBusFree();
	}


	// Releases both lines. A held SCL is busy. A held SDA gets nine clocks and a STOP, then busy if it is still held. SDA is read
	// after a half bit, so a slow rise through the pull-up is not taken for a hold. That half bit, and the one before a START,
	// leave the bus free for longer than the Fast-mode tBUF after a STOP.
	ResultStatus ReleaseBus() {
		SetSda(Line::High);
		SetScl(Line::High);

		ResultStatus status = WaitScl(Line::High);
		if (status != ResultStatus::ok) {
			return ResultStatus::busy;
		}

		HoldHigh();
		if (ReadSda() == 0) {
			return RecoverBus();
		}

		return ResultStatus::ok;
	}


	ResultStatus RecoverBus() {
		SetSda(Line::High);

		for (uint8 pulse = 0; pulse < recoveryClocks; pulse++) {
			SetScl(Line::Low);
			if (ClockHigh() != ResultStatus::ok) {
				return ResultStatus::busy;
			}
		}

		if (LLStop() != ResultStatus::ok) {
			return ResultStatus::busy;
		}

		return ResultStatus::ok;
	}


	// START: SDA falls while SCL is high, then SCL falls. A START that cannot be checked sends nothing more.
	ResultStatus SendStart() {
		SetSda(Line::Low);
		HoldHigh();
		SetScl(Line::Low);

		if (ReadSda() != 0 || ReadScl() != 0) {
			return ResultStatus::busy;
		}

		return ResultStatus::ok;
	}


	// Raises SCL after the low half of a bit and holds the high half. A bit is the requested period in total.
	ResultStatus ClockHigh() {
		HoldLow();
		SetScl(Line::High);

		ResultStatus status = WaitScl(Line::High);
		if (status != ResultStatus::ok) {
			return status;
		}

		HoldHigh();
		return ResultStatus::ok;
	}


	// A transmitted 1 must read back as 1. A low SDA means another device holds the line, so the bit is busy. A target does not
	// drive SDA during a master's address or data bits, and it releases SDA for the master's ACK or NACK bit.
	ResultStatus WriteBit(uint8 bit) {
		SetSda(bit != 0 ? Line::High : Line::Low);

		ResultStatus status = ClockHigh();
		if (status == ResultStatus::ok && bit != 0 && ReadSda() == 0) {
			status = ResultStatus::busy;
		}

		SetScl(Line::Low);
		return status;
	}


	ResultStatus ReadBit(uint8 &bit) {
		SetSda(Line::High);

		ResultStatus status = ClockHigh();
		if (status == ResultStatus::ok) {
			bit = ReadSda();
		}

		SetScl(Line::Low);
		return status;
	}


	ResultStatus WriteByte(uint8 data) {
		if (IsTransferExpired()) {
			return ResultStatus::timeout;
		}

		for (uint8 index = 0; index < 8; index++) {
			ResultStatus status = WriteBit(static_cast<uint8>((data >> (7 - index)) & 1));
			if (status != ResultStatus::ok) {
				return status;
			}
		}

		uint8 ackBit = 1;
		ResultStatus status = ReadBit(ackBit);
		if (status != ResultStatus::ok) {
			return status;
		}

		if (ackBit != 0) {
			return ResultStatus::nack;
		}

		return ResultStatus::ok;
	}


	ResultStatus ReadByte(uint8 &value, bool ack) {
		if (IsTransferExpired()) {
			return ResultStatus::timeout;
		}

		uint8 result = 0;
		for (uint8 index = 0; index < 8; index++) {
			uint8 bit = 0;
			ResultStatus status = ReadBit(bit);
			if (status != ResultStatus::ok) {
				return status;
			}

			result = static_cast<uint8>((result << 1) | bit);
		}

		ResultStatus status = WriteBit(static_cast<uint8>(ack ? 0 : 1));
		if (status == ResultStatus::ok) {
			value = result;
		}

		return status;
	}


	// Waits for SCL to reach the wanted level. Stops at the per-wait timeout or the transfer deadline, whichever is first.
	ResultStatus WaitScl(Line waitState) {
		uint64 start = System::GetTick();
		while (ReadScl() != static_cast<uint8>(waitState)) {
			if (IsWaitExpired(start)) {
				return ResultStatus::timeout;
			}
		}

		return ResultStatus::ok;
	}


	ResultStatus WaitBusFree() {
		uint64 start = System::GetTick();
		while (ReadScl() != static_cast<uint8>(Line::High) || ReadSda() != static_cast<uint8>(Line::High)) {
			if (IsWaitExpired(start)) {
				return ResultStatus::busy;
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


	// The halves of a bit add up to the bit period. Up to 100 kHz they are equal, which meets the Standard-mode minimums. Above
	// that the low half is two thirds of the period, rounded up, which meets the Fast-mode minimum low time of 1.3 us. The high
	// half is then the rest, and it meets the Fast-mode minimum of 0.6 us.
	void HoldLow() {
		Hold(LowPeriodUs());
	}


	void HoldHigh() {
		Hold(bitPeriodUs - LowPeriodUs());
	}


	uint32 LowPeriodUs() {
		if (bitPeriodUs >= standardModePeriodUs) {
			return bitPeriodUs / 2;
		}

		return (2 * bitPeriodUs + 2) / 3;
	}


	void Hold(uint32 delayUs) {
		if (delayUs != 0) {
			System::DelayUs(delayUs);
		}
	}
};
