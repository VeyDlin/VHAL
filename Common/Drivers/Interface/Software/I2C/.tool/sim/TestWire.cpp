#include <VHAL.h>
#include "FakeBus.h"
#include "SimCheck.h"
#include <SoftwareI2C.h>
#include <string>


// Wire bytes of writes and reads, ACK and NACK, the repeated START, the parameter and device checks, and the result of each
// call. Each case builds its own bus and compares the trace the target saw with the expected sequence.


namespace {


void WriteSixteenBitAddressHighByteFirst() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	uint8 data = 0x55;
	ResultStatus status = bus.WriteByteArray(0x48, 0x1234, 2, &data, 1);
	SIM_EQ(status, ResultStatus::ok);
	SIM_TEXT(TraceText(), "S 90w+ 12w+ 34w+ 55w+ P");
}


void WriteOneByteAddress() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	uint8 data[2] = { 0xAA, 0x55 };
	ResultStatus status = bus.WriteByteArray(0x48, 0x34, 1, data, 2);
	SIM_EQ(status, ResultStatus::ok);
	SIM_TEXT(TraceText(), "S 90w+ 34w+ AAw+ 55w+ P");
}


void WriteWithoutRegisterAddress() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	uint8 data = 0x77;
	ResultStatus status = bus.WriteByteArray(0x48, 0x0000, 0, &data, 1);
	SIM_EQ(status, ResultStatus::ok);
	SIM_TEXT(TraceText(), "S 90w+ 77w+ P");
}


void WriteAddressOnlyWhenNoData() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	ResultStatus status = bus.WriteByteArray(0x48, 0x0102, 2, nullptr, 0);
	SIM_EQ(status, ResultStatus::ok);
	SIM_TEXT(TraceText(), "S 90w+ 01w+ 02w+ P");
}


void AddressSizeAboveTwoIsRejected() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	uint8 data = 0x55;
	ResultStatus status = bus.Write<uint8, uint32>(0x48, 0x12345678u, data);
	SIM_EQ(status, ResultStatus::invalidParameter);
	SIM_TEXT(TraceText(), "");
}


void AddressWiderThanSizeIsRejected() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	uint8 data = 0x55;
	ResultStatus status = bus.WriteByteArray(0x48, 0x0123, 1, &data, 1);
	SIM_EQ(status, ResultStatus::invalidParameter);
	SIM_TEXT(TraceText(), "");
}


void DeviceAbove7FIsRejected() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	uint8 data = 0x55;
	ResultStatus writeStatus = bus.WriteByteArray(0x80, 0x01, 1, &data, 1);
	uint8 buffer = 0;
	ResultStatus readStatus = bus.ReadByteArray(0xFF, 0x01, 1, &buffer, 1);
	SIM_EQ(writeStatus, ResultStatus::invalidParameter);
	SIM_EQ(readStatus, ResultStatus::invalidParameter);
	SIM_TEXT(TraceText(), "");
}


void ReadTwoBytesUsesRepeatedStart() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	simBus.readBytes = { 0xA5, 0x5A };

	uint8 buffer[2] = { 0, 0 };
	ResultStatus status = bus.ReadByteArray(0x48, 0x1234, 2, buffer, 2);
	SIM_EQ(status, ResultStatus::ok);
	SIM_EQ(buffer[0], 0xA5);
	SIM_EQ(buffer[1], 0x5A);
	SIM_TEXT(TraceText(), "S 90w+ 12w+ 34w+ Sr 91w+ A5r+ 5Ar- P");
}


void ReadWithoutRegisterAddressSkipsWritePhase() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	simBus.readBytes = { 0x3C };

	uint8 buffer = 0;
	ResultStatus status = bus.ReadByteArray(0x48, 0x0000, 0, &buffer, 1);
	SIM_EQ(status, ResultStatus::ok);
	SIM_EQ(buffer, 0x3C);
	SIM_TEXT(TraceText(), "S 91w+ 3Cr- P");
}


void ReadTemplatesReturnTheBytes() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	simBus.readBytes = { 0x42, 0x01, 0x02, 0x03 };

	Result<uint8> single = bus.Read<uint8, uint8>(0x48, 0x00);
	SIM_CHECK(single.IsOk());
	SIM_EQ(single.Value(), 0x42);

	// Every START makes the target send from its first byte again.
	uint8 burst[3] = { 0, 0, 0 };
	ResultStatus status = bus.ReadArray<uint8, uint8>(0x48, 0x10, burst, 3);
	SIM_EQ(status, ResultStatus::ok);
	SIM_EQ(burst[0], 0x42);
	SIM_EQ(burst[1], 0x01);
	SIM_EQ(burst[2], 0x02);
}


void ClassExposesTemplatesAndByteWrite() {
	// Compile-time check: the non-template Read and Write do not hide the base templates, and Write(uint8) still exists.
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	simBus.readBytes = { 0x11 };

	Result<uint8> value = bus.Read<uint8, uint8>(0x48, 0x00);
	SIM_CHECK(value.IsOk());

	ResultStatus (SoftwareI2C::*writeByte)(uint8) = &SoftwareI2C::Write;
	SIM_CHECK(writeByte != nullptr);
}


void DataNackIsReportedAndStopped() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	// Counts the bytes after the device address, the register address first. Index 2 is the third byte, the second data byte.
	simBus.nackDataIndex = 2;

	int errorCount = 0;
	SoftwareI2C::Error lastError = SoftwareI2C::Error::None;
	bus.onError = [&](SoftwareI2C::Error error) -> void {
		errorCount++;
		lastError = error;
	};

	uint8 data[3] = { 1, 2, 3 };
	ResultStatus status = bus.WriteByteArray(0x48, 0x10, 1, data, 3);
	SIM_EQ(status, ResultStatus::nack);
	SIM_EQ(errorCount, 1);
	SIM_EQ(lastError, SoftwareI2C::Error::AcknowledgeFailure);
	SIM_TEXT(TraceText(), "S 90w+ 10w+ 01w+ 02w- P");
}


void AddressNackIsReportedAndStopped() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	simBus.targetPresent = false;

	int errorCount = 0;
	bus.onError = [&](SoftwareI2C::Error error) -> void {
		if (error == SoftwareI2C::Error::AcknowledgeFailure) {
			errorCount++;
		}
	};

	uint8 data = 0x01;
	ResultStatus writeStatus = bus.WriteByteArray(0x48, 0x10, 1, &data, 1);
	SIM_EQ(writeStatus, ResultStatus::nack);
	SIM_TEXT(TraceText(), "S 90w- P");

	simBus.events.clear();
	uint8 buffer = 0;
	ResultStatus readStatus = bus.ReadByteArray(0x48, 0x10, 1, &buffer, 1);
	SIM_EQ(readStatus, ResultStatus::nack);
	SIM_TEXT(TraceText(), "S 90w- P");
	SIM_EQ(errorCount, 2);
}


void CheckDeviceAndScanUseTheAddressPhase() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	SIM_EQ(bus.CheckDevice(0x48, 1), ResultStatus::ok);
	SIM_TEXT(TraceText(), "S 90w+ P");

	simBus.events.clear();
	SIM_EQ(bus.CheckDevice(0x49, 2), ResultStatus::nack);
	SIM_TEXT(TraceText(), "S 92w- P S 92w- P");

	simBus.events.clear();
	uint8 found[4] = { 0, 0, 0, 0 };
	Result<uint8> count = bus.Scan(found, 4);
	SIM_CHECK(count.IsOk());
	SIM_EQ(count.Value(), 1);
	SIM_EQ(found[0], 0x48);
}


void ScanWithZeroSizeFindsNothing() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	Result<uint8> count = bus.Scan(nullptr, 0);
	SIM_CHECK(count.IsOk());
	SIM_EQ(count.Value(), 0);
	SIM_TEXT(TraceText(), "");
}


void AsyncTransfersAreNotSupported() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	uint8 data = 0;
	SIM_EQ(bus.CheckDeviceAsync(0x48, 1), ResultStatus::notSupported);
	SIM_EQ(bus.WriteByteArrayAsync(0x48, 0, 1, &data, 1), ResultStatus::notSupported);
	SIM_EQ(bus.ReadByteArrayAsync(0x48, 0, 1, &data, 1), ResultStatus::notSupported);
	SIM_EQ(bus.ScanAsync(&data, 1).IsErr(), 1);
	SIM_TEXT(TraceText(), "");
}


void UnsupportedParametersAreRefused() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	SoftwareI2C::Parameters slave;
	slave.mode = SoftwareI2C::Mode::Slave;
	SIM_EQ(bus.SetParameters(slave), ResultStatus::notSupported);

	SoftwareI2C::Parameters tenBit;
	tenBit.addressingMode = SoftwareI2C::AddressingMode::B10;
	SIM_EQ(bus.SetParameters(tenBit), ResultStatus::notSupported);

	SoftwareI2C::Parameters noStretch;
	noStretch.clockStretching = false;
	SIM_EQ(bus.SetParameters(noStretch), ResultStatus::notSupported);

	SoftwareI2C::Parameters analog;
	analog.analogFilter = true;
	SIM_EQ(bus.SetParameters(analog), ResultStatus::notSupported);

	SoftwareI2C::Parameters digital;
	digital.digitalFilter = 2;
	SIM_EQ(bus.SetParameters(digital), ResultStatus::notSupported);

	SoftwareI2C::Parameters fastPlus;
	fastPlus.speed = SoftwareI2C::Speed::FastPlus;
	SIM_EQ(bus.SetParameters(fastPlus), ResultStatus::notSupported);

	SoftwareI2C::Parameters fast;
	fast.speed = SoftwareI2C::Speed::Fast;
	SIM_EQ(bus.SetParameters(fast), ResultStatus::ok);
}


void SetFrequencyRejectsZeroAndAboveFastMode() {
	// 400 kHz is the top of Fast mode. Above it, a bit of one microsecond has no low half the I2C minimum allows.
	SoftwareI2C bus;
	SIM_EQ(bus.SetFrequency(0), ResultStatus::invalidParameter);
	SIM_EQ(bus.SetFrequency(401), ResultStatus::invalidParameter);
	SIM_EQ(bus.SetFrequency(1000), ResultStatus::invalidParameter);
	SIM_EQ(bus.SetFrequency(400), ResultStatus::ok);
	SIM_EQ(bus.SetFrequency(100), ResultStatus::ok);
}


void TransfersBeforeSetPinsFailCleanly() {
	ResetSim(0);
	SoftwareI2C bus;

	uint8 data = 0x55;
	SIM_EQ(bus.WriteByteArray(0x48, 0x01, 1, &data, 1), ResultStatus::noInit);
	uint8 buffer = 0;
	SIM_EQ(bus.ReadByteArray(0x48, 0x01, 1, &buffer, 1), ResultStatus::noInit);
	SIM_EQ(bus.CheckDevice(0x48, 1), ResultStatus::noInit);
	SIM_EQ(bus.Open(), ResultStatus::noInit);
	SIM_EQ(bus.Free(), ResultStatus::noInit);
	SIM_CHECK(simBus.pinCalls == 0);
	SIM_TEXT(TraceText(), "");
}


void DefaultConstructedBusTouchesNoPins() {
	ResetSim(0);
	{
		SoftwareI2C bus;
	}
	SIM_CHECK(simBus.pinCalls == 0);
}


}


int main() {
	WriteSixteenBitAddressHighByteFirst();
	WriteOneByteAddress();
	WriteWithoutRegisterAddress();
	WriteAddressOnlyWhenNoData();
	AddressSizeAboveTwoIsRejected();
	AddressWiderThanSizeIsRejected();
	DeviceAbove7FIsRejected();
	ReadTwoBytesUsesRepeatedStart();
	ReadWithoutRegisterAddressSkipsWritePhase();
	ReadTemplatesReturnTheBytes();
	ClassExposesTemplatesAndByteWrite();
	DataNackIsReportedAndStopped();
	AddressNackIsReportedAndStopped();
	CheckDeviceAndScanUseTheAddressPhase();
	ScanWithZeroSizeFindsNothing();
	AsyncTransfersAreNotSupported();
	UnsupportedParametersAreRefused();
	SetFrequencyRejectsZeroAndAboveFastMode();
	TransfersBeforeSetPinsFailCleanly();
	DefaultConstructedBusTouchesNoPins();
	return SimCheck::Finish("TestWire");
}
