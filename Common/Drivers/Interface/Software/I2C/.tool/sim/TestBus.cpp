#include <VHAL.h>
#include "FakeBus.h"
#include "SimCheck.h"
#include <SoftwareI2C.h>


// Pin setup, a held SDA or SCL, the nine-clock recovery and its STOP, the busy results, and the per-wait and total timeouts.
// The polling loops run in simulated time (a GetTick call costs one microsecond), so a held line times out.


namespace {


void SetPinsReleasesTheBus() {
	ResetSim(1);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	SIM_CHECK(simBus.masterScl);
	SIM_CHECK(simBus.masterSda);
	SIM_TEXT(TraceText(), "");
}


void SetPinsPropagatesPinConfigurationErrors() {
	ResetSim(1);
	simBus.failPinConfig = true;
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::hardwareFailure);

	uint8 data = 0x01;
	SIM_EQ(bus.WriteByteArray(0x48, 0x01, 1, &data, 1), ResultStatus::noInit);
}


void SetPinsReportsStuckSclAsBusy() {
	ResetSim(1);
	SoftwareI2C bus;
	bus.SetTimeout(5);
	simBus.stuckScl = true;
	SIM_EQ(AttachPins(bus), ResultStatus::busy);
	SIM_TEXT(TraceText(), "");
}


void SetPinsRecoversSdaWithNineClocksAndStop() {
	ResetSim(1);
	simBus.sdaHeldUntilRise = 3;
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	// Nine recovery clocks, then the clock of the STOP. The STOP is the only bus event.
	SIM_EQ(simBus.riseCount, 10);
	SIM_TEXT(TraceText(), "P");
}


void SetPinsReportsStuckSdaAsBusy() {
	ResetSim(1);
	simBus.stuckSdaForever = true;
	SoftwareI2C bus;
	bus.SetTimeout(5);
	SIM_EQ(AttachPins(bus), ResultStatus::busy);
	SIM_EQ(simBus.riseCount, 10);
	SIM_TEXT(TraceText(), "");
}


void FreeReportsHeldSdaAsBusy() {
	// A held SDA must not count as a free bus, even while SCL is high.
	ResetSim(1);
	SoftwareI2C bus;
	bus.SetTimeout(5);
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	simBus.stuckSdaForever = true;
	SIM_EQ(bus.Free(), ResultStatus::busy);
}


void WriteRecoversHeldSdaBeforeStart() {
	ResetSim(1);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	simBus.sdaHeldUntilRise = simBus.riseCount + 3;
	uint8 data = 0x55;
	SIM_EQ(bus.WriteByteArray(0x48, 0x01, 1, &data, 1), ResultStatus::ok);
	SIM_TEXT(TraceText(), "P S 90w+ 01w+ 55w+ P");
}


void WriteWithHeldSdaIsBusyAndSendsNoStart() {
	ResetSim(1);
	SoftwareI2C bus;
	bus.SetTimeout(5);
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	simBus.stuckSdaForever = true;
	uint8 data = 0x55;
	SIM_EQ(bus.WriteByteArray(0x48, 0x01, 1, &data, 1), ResultStatus::busy);
	SIM_TEXT(TraceText(), "");
}


void FailedStartNeverSendsStop() {
	ResetSim(1);
	SoftwareI2C bus;
	bus.SetTimeout(5);
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	simBus.stuckScl = true;
	uint8 buffer = 0;
	SIM_EQ(bus.ReadByteArray(0x48, 0x01, 1, &buffer, 1), ResultStatus::busy);
	SIM_TEXT(TraceText(), "");
}


void PerWaitTimeoutEndsTheTransfer() {
	ResetSim(1);
	SoftwareI2C bus;
	bus.SetTimeout(20);
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	simBus.stretchUs = 50000;
	uint8 data = 0x55;
	SIM_EQ(bus.WriteByteArray(0x48, 0x01, 1, &data, 1), ResultStatus::timeout);
	SIM_TEXT(TraceText(), "S");
}


void TotalTimeoutLimitsAStretchedTransfer() {
	// Each SCL release is stretched by 30 ms. The per-wait limit is 1 s, so only the 100 ms total can stop the transfer.
	ResetSim(1);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	SIM_EQ(bus.SetTransferTimeout(100), ResultStatus::ok);

	simBus.stretchUs = 30000;
	uint8 data[4] = { 1, 2, 3, 4 };
	uint64 startMs = simNowUs / 1000;
	ResultStatus status = bus.WriteByteArray(0x48, 0x01, 1, data, 4);
	uint64 elapsedMs = simNowUs / 1000 - startMs;

	SIM_EQ(status, ResultStatus::timeout);
	SIM_CHECK(elapsedMs <= 130);
	SIM_CHECK(elapsedMs >= 100);
}


void TransferTimeoutRejectsZero() {
	SoftwareI2C bus;
	SIM_EQ(bus.SetTransferTimeout(0), ResultStatus::invalidParameter);
}


void FailedStopReleasesSda() {
	// SCL is held low by a device after the START. The STOP cannot finish, and it must not leave SDA driven low, which a
	// released SCL would then turn into a START.
	ResetSim(1);
	SoftwareI2C bus;
	bus.SetTimeout(5);
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	SIM_EQ(bus.Open(), ResultStatus::ok);

	simBus.stuckScl = true;
	SIM_EQ(bus.Close(), ResultStatus::timeout);
	SIM_CHECK(simBus.masterSda);
	SIM_TEXT(TraceText(), "S");
}


void RestartWithHeldSdaIsBusy() {
	// The target holds SDA low before the repeated START. A repeated START on a held line sends nothing.
	ResetSim(1);
	SoftwareI2C bus;
	bus.SetTimeout(5);
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	SIM_EQ(bus.Open(), ResultStatus::ok);
	SIM_EQ(bus.RequestWrite(0x48), ResultStatus::ok);

	simBus.stuckSdaForever = true;
	SIM_EQ(bus.Restart(), ResultStatus::busy);
	SIM_TEXT(TraceText(), "S 90w+");
}


void WriteWithSdaHeldDuringDataIsBusy() {
	// Writes 0xFF with no register address. SDA is held low from the tenth SCL rise, which is the first data bit. A transmitted
	// 1 that reads back low means another device holds the line, so the write is busy and sends nothing more.
	ResetSim(1);
	SoftwareI2C bus;
	bus.SetTimeout(5);
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	simBus.sdaHeldFromRise = 10;
	simBus.sdaHeldUntilRise = 1000;
	uint8 data = 0xFF;
	SIM_EQ(bus.WriteByteArray(0x48, 0x00, 0, &data, 1), ResultStatus::busy);
	SIM_TEXT(TraceText(), "S 90w+");
}


void TransferDeadlineStartsAtRequest() {
	// The total limit covers a transfer. Time between Open and the request is not part of it.
	ResetSim(1);
	SoftwareI2C bus;
	bus.SetTimeout(5);
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	SIM_EQ(bus.SetTransferTimeout(100), ResultStatus::ok);
	SIM_EQ(bus.Open(), ResultStatus::ok);

	simNowUs += 2000000;
	SIM_EQ(bus.RequestWrite(0x48), ResultStatus::ok);
	SIM_EQ(bus.Close(), ResultStatus::ok);
	SIM_TEXT(TraceText(), "S 90w+ P");
}


}


int main() {
	SetPinsReleasesTheBus();
	SetPinsPropagatesPinConfigurationErrors();
	SetPinsReportsStuckSclAsBusy();
	SetPinsRecoversSdaWithNineClocksAndStop();
	SetPinsReportsStuckSdaAsBusy();
	FreeReportsHeldSdaAsBusy();
	WriteRecoversHeldSdaBeforeStart();
	WriteWithHeldSdaIsBusyAndSendsNoStart();
	FailedStartNeverSendsStop();
	PerWaitTimeoutEndsTheTransfer();
	TotalTimeoutLimitsAStretchedTransfer();
	TransferTimeoutRejectsZero();
	FailedStopReleasesSda();
	RestartWithHeldSdaIsBusy();
	WriteWithSdaHeldDuringDataIsBusy();
	TransferDeadlineStartsAtRequest();
	return SimCheck::Finish("TestBus");
}
