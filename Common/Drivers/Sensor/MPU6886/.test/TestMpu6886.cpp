#include <VHAL.h>
#include <Drivers/Sensor/MPU6886/MPU6886.h>
#include <VhalCheck.h>
#include <utility>
#include <vector>


// Host tests of the MPU6886 driver: the decoding and scaling, the register sequence, the readback check, the gyroscope bias
// calibration and the FIFO, against the simulated device of the module shim (shim/VHAL.h).


namespace {


void CheckDecode() {
	// Big-endian pairs in burst order: accel X, Y, Z, temperature, gyro X, Y, Z.
	uint8 raw[14] = {
		0x20, 0x00,
		0xE0, 0x00,
		0x10, 0x00,
		0x0C, 0xC4,
		0x02, 0x8F,
		0xFF, 0x7D,
		0x00, 0x00
	};
	MPU6886::Sample sample;
	MPU6886::Decode(raw, MPU6886::AccelLsbPerG(MPU6886::AccelRange::G4), MPU6886::GyroLsbPerDps(MPU6886::GyroRange::Dps500),
		sample);
	VHAL_CHECK_NEAR(sample.accelX, 1.0f, 0.001f);
	VHAL_CHECK_NEAR(sample.accelY, -1.0f, 0.001f);
	VHAL_CHECK_NEAR(sample.accelZ, 0.5f, 0.001f);
	VHAL_CHECK_NEAR(sample.temperatureC, 35.0f, 0.01f);
	VHAL_CHECK_NEAR(sample.gyroX, 10.0f, 0.01f);
	VHAL_CHECK_NEAR(sample.gyroY, -2.0f, 0.01f);
	VHAL_CHECK_NEAR(sample.gyroZ, 0.0f, 0.01f);

	VHAL_CHECK_NEAR(MPU6886::AccelLsbPerG(MPU6886::AccelRange::G16), 2048.0f, 0.0f);
	VHAL_CHECK_NEAR(MPU6886::GyroLsbPerDps(MPU6886::GyroRange::Dps2000), 16.4f, 0.0f);
}


void CheckInitSequence() {
	AI2C bus;
	bus.regs[0x75] = 0x19;
	MPU6886 imu(bus);
	ResultStatus status = imu.Init();
	VHAL_CHECK(status == ResultStatus::ok);

	std::vector<std::pair<uint8, uint8>> expected = {
		{ 0x6B, 0x80 },
		{ 0x6B, 0x01 },
		{ 0x6C, 0x00 },
		{ 0x1A, 0x03 },
		{ 0x19, 0x00 },
		{ 0x1B, 0x08 },
		{ 0x1C, 0x08 },
		{ 0x1D, 0x02 }
	};
	VHAL_CHECK(bus.writes == expected);
}


void CheckReadScaled() {
	AI2C bus;
	bus.regs[0x75] = 0x19;
	MPU6886 imu(bus);
	imu.Init();

	uint8 burst[14] = { 0x20, 0x00, 0xE0, 0x00, 0x10, 0x00, 0x0C, 0xC4, 0x02, 0x8F, 0xFF, 0x7D, 0x00, 0x00 };
	for (uint32 index = 0; index < 14; index++) {
		bus.regs[0x3B + index] = burst[index];
	}

	Result<MPU6886::Sample> read = imu.Read();
	VHAL_CHECK(read.IsOk());
	if (read.IsOk()) {
		VHAL_CHECK_NEAR(read.Value().accelX, 1.0f, 0.001f);
		VHAL_CHECK_NEAR(read.Value().gyroX, 10.0f, 0.01f);
		VHAL_CHECK_NEAR(read.Value().temperatureC, 35.0f, 0.01f);
	}
}


void CheckWrongWhoAmI() {
	AI2C bus;
	bus.regs[0x75] = 0x70;
	MPU6886 imu(bus);
	VHAL_CHECK(imu.Init() == ResultStatus::notRecognized);
}


void CheckReadbackMismatch() {
	AI2C bus;
	bus.regs[0x75] = 0x19;
	bus.droppedWriteReg = 0x1A;
	MPU6886 imu(bus);
	VHAL_CHECK(imu.Init() == ResultStatus::writeError);
}


void CheckGyroBias() {
	AI2C bus;
	bus.regs[0x75] = 0x19;
	MPU6886 imu(bus);
	imu.Init();

	uint8 burst[14] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x21, 0, 0, 0, 0 };
	for (uint32 index = 0; index < 14; index++) {
		bus.regs[0x3B + index] = burst[index];
	}

	MPU6886::GyroBias bias;
	ResultStatus status = imu.CalibrateGyroBias(20, 10, 2.0f, bias);
	VHAL_CHECK(status == ResultStatus::ok);
	VHAL_CHECK_NEAR(bias.x, 0.503f, 0.01f);
}


void CheckGyroBiasRejectsMotion() {
	AI2C bus;
	bus.regs[0x75] = 0x19;
	bus.swingGyroX = true;
	MPU6886 imu(bus);
	imu.Init();

	MPU6886::GyroBias bias;
	ResultStatus status = imu.CalibrateGyroBias(20, 10, 2.0f, bias);
	VHAL_CHECK(status == ResultStatus::busy);
}


// FIFO: three frames in the FIFO are decoded in order and removed; the write sequence of the start is checked too.
void CheckFifoDrain() {
	AI2C bus;
	bus.regs[0x75] = 0x19;
	MPU6886 imu(bus);
	imu.Init();
	imu.StartFifo();

	uint8 frame[14] = { 0x20, 0x00, 0xE0, 0x00, 0x10, 0x00, 0x0C, 0xC4, 0x02, 0x8F, 0xFF, 0x7D, 0x00, 0x00 };
	for (uint32 index = 0; index < 3; index++) {
		bus.fifo.insert(bus.fifo.end(), frame, frame + 14);
	}

	MPU6886::Sample frames[16];
	uint32 count = 0;
	bool overflowed = true;
	ResultStatus status = imu.ReadFifo(frames, count, overflowed);
	VHAL_CHECK(status == ResultStatus::ok);
	VHAL_CHECK(count == 3 && !overflowed);
	VHAL_CHECK_NEAR(frames[2].accelX, 1.0f, 0.001f);
	VHAL_CHECK_NEAR(frames[0].gyroX, 10.0f, 0.01f);
	VHAL_CHECK(bus.fifo.empty());

	std::vector<std::pair<uint8, uint8>> expected = {
		{ 0x6B, 0x80 },
		{ 0x6B, 0x01 },
		{ 0x6C, 0x00 },
		{ 0x1A, 0x03 },
		{ 0x19, 0x00 },
		{ 0x1B, 0x08 },
		{ 0x1C, 0x08 },
		{ 0x1D, 0x02 },
		{ 0x23, 0x18 },
		{ 0x6A, 0x44 }
	};
	VHAL_CHECK(bus.writes == expected);
}


void CheckSleep() {
	AI2C bus;
	bus.regs[0x75] = 0x19;
	MPU6886 imu(bus);
	VHAL_CHECK(imu.Sleep() == ResultStatus::ok);

	std::vector<std::pair<uint8, uint8>> expected = { { 0x6B, 0x41 } };
	VHAL_CHECK(bus.writes == expected);
	VHAL_CHECK(bus.regs[0x6B] == 0x41);
}


// An overflow resets the FIFO and returns no frames, and the FIFO reset is written last.
void CheckFifoOverflow() {
	AI2C bus;
	bus.regs[0x75] = 0x19;
	MPU6886 imu(bus);
	imu.Init();
	imu.StartFifo();

	uint8 frame[14] = { 0 };
	bus.fifo.insert(bus.fifo.end(), frame, frame + 14);
	bus.regs[0x3A] = 0x10;

	MPU6886::Sample frames[16];
	uint32 count = 7;
	bool overflowed = false;
	imu.ReadFifo(frames, count, overflowed);
	VHAL_CHECK(overflowed);
	VHAL_CHECK(count == 0);
	VHAL_CHECK(bus.fifo.empty());
	VHAL_CHECK(bus.writes.back() == std::make_pair(static_cast<uint8>(0x6A), static_cast<uint8>(0x44)));
}


}


int main() {
	CheckDecode();
	CheckInitSequence();
	CheckReadScaled();
	CheckWrongWhoAmI();
	CheckReadbackMismatch();
	CheckGyroBias();
	CheckGyroBiasRejectsMotion();
	CheckFifoDrain();
	CheckSleep();
	CheckFifoOverflow();
	return VhalCheck::Finish("TestMpu6886");
}
