#pragma once
#include <VHAL.h>


// InvenSense/TDK MPU-6886 accelerometer and gyroscope on I2C, as on the M5StickC PLUS2 (I2C0, SDA 21, SCL 22, address
// 0x68). The INT pin is not connected to the ESP32 on this board, so every read is polled and no interrupt is set up.
// The caller starts the bus (AI2C::SetParameters) before Init; the driver never reconfigures the bus.
class MPU6886 {
public:
	// The enumerator values are the AFS_SEL and FS_SEL fields (bits 4:3 of ACCEL_CONFIG and GYRO_CONFIG).
	enum class AccelRange : uint8 {
		G2 = 0,
		G4 = 1,
		G8 = 2,
		G16 = 3
	};


	enum class GyroRange : uint8 {
		Dps250 = 0,
		Dps500 = 1,
		Dps1000 = 2,
		Dps2000 = 3
	};


	struct Config {
		AccelRange accelRange = AccelRange::G4;
		GyroRange gyroRange = GyroRange::Dps500;
		// CONFIG.DLPF_CFG, 0 to 6. 3 gives a 41 Hz gyroscope bandwidth at the 1 kHz internal rate.
		uint8 gyroDlpf = 3;
		// ACCEL_CONFIG2.A_DLPF_CFG, 0 to 6. 2 gives a 99 Hz accelerometer bandwidth at the 1 kHz internal rate, which
		// keeps the 5 to 20 ms taps of the tap detector sharper than 44.8 Hz (3) does.
		uint8 accelDlpf = 2;
		// The output rate is 1 kHz / (1 + sampleRateDivider) while the DLPF is on. 0 gives 1 kHz, the FIFO rate used by
		// the tap detector. Tilt does not need the full rate; it just receives every frame.
		uint8 sampleRateDivider = 0;
	};


	struct Sample {
		float accelX = 0.0f;
		float accelY = 0.0f;
		float accelZ = 0.0f;
		float gyroX = 0.0f;
		float gyroY = 0.0f;
		float gyroZ = 0.0f;
		float temperatureC = 0.0f;
	};


	struct GyroBias {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};


	static constexpr uint8 defaultAddress = 0x68;
	static constexpr uint8 whoAmIValue = 0x19;
	static constexpr uint32 burstBytes = 14;
	// One FIFO frame is the same 14 bytes as the burst read, so ReadFifo fills at most this many frames per call.
	static constexpr uint32 fifoMaxFrames = 16;


private:
	static constexpr uint8 regSmplrtDiv = 0x19;
	static constexpr uint8 regConfig = 0x1A;
	static constexpr uint8 regGyroConfig = 0x1B;
	static constexpr uint8 regAccelConfig = 0x1C;
	static constexpr uint8 regAccelConfig2 = 0x1D;
	static constexpr uint8 regIntStatus = 0x3A;
	static constexpr uint8 regAccelXoutH = 0x3B;
	static constexpr uint8 regFifoEn = 0x23;
	static constexpr uint8 regUserCtrl = 0x6A;
	static constexpr uint8 regPwrMgmt1 = 0x6B;
	static constexpr uint8 regPwrMgmt2 = 0x6C;
	static constexpr uint8 regFifoCountH = 0x72;
	static constexpr uint8 regFifoCountL = 0x73;
	static constexpr uint8 regFifoRw = 0x74;
	static constexpr uint8 regWhoAmI = 0x75;

	static constexpr uint8 pwrMgmt1Reset = 0x80;
	static constexpr uint8 pwrMgmt1Wake = 0x01;
	static constexpr uint8 pwrMgmt2AllAxes = 0x00;
	static constexpr uint8 dlpfMask = 0x07;
	static constexpr uint8 fifoEnGyroAndAccel = 0x18;
	static constexpr uint8 userCtrlFifoEnable = 0x40;
	static constexpr uint8 userCtrlFifoReset = 0x04;
	static constexpr uint8 intStatusFifoOverflow = 0x10;
	static constexpr uint32 fifoCapacityBytes = 1024;

	// Datasheet: the device comes up asleep, and the gyroscope needs up to 100 ms (35 ms typical) after the wake-up.
	// The reset delay is not given in the datasheet; 100 ms is a conservative choice.
	static constexpr uint32 resetDelayMs = 100;
	static constexpr uint32 wakeDelayMs = 100;

	static constexpr float accelLsbPerGTable[4] = { 16384.0f, 8192.0f, 4096.0f, 2048.0f };
	static constexpr float gyroLsbPerDpsTable[4] = { 131.0f, 65.5f, 32.8f, 16.4f };
	static constexpr float temperatureLsbPerC = 326.8f;
	static constexpr float temperatureOffsetC = 25.0f;

	AI2C& bus;
	uint8 address = defaultAddress;
	float accelLsbPerG = accelLsbPerGTable[1];
	float gyroLsbPerDps = gyroLsbPerDpsTable[1];


public:
	MPU6886(AI2C& i2cBus, uint8 deviceAddress = defaultAddress)
		: bus(i2cBus), address(deviceAddress) { }


	static float AccelLsbPerG(AccelRange range) {
		return accelLsbPerGTable[static_cast<uint8>(range)];
	}


	static float GyroLsbPerDps(GyroRange range) {
		return gyroLsbPerDpsTable[static_cast<uint8>(range)];
	}


	ResultStatus Init() {
		return Init(Config());
	}


	// Resets the device, wakes it, checks WHO_AM_I and writes the configuration. Every configuration register is read
	// back and compared. The scales are changed only when the whole sequence succeeds.
	ResultStatus Init(const Config& config) {
		ResultStatus status = bus.Write<uint8, uint8>(address, regPwrMgmt1, pwrMgmt1Reset);
		if (status != ResultStatus::ok) {
			return status;
		}
		System::DelayMs(resetDelayMs);

		Result<uint8> whoAmI = bus.Read<uint8, uint8>(address, regWhoAmI);
		if (whoAmI.IsErr()) {
			return whoAmI.Error();
		}
		if (whoAmI.Value() != whoAmIValue) {
			return ResultStatus::notRecognized;
		}

		status = WriteVerified(regPwrMgmt1, pwrMgmt1Wake);
		if (status != ResultStatus::ok) {
			return status;
		}
		System::DelayMs(wakeDelayMs);

		status = WriteVerified(regPwrMgmt2, pwrMgmt2AllAxes);
		if (status != ResultStatus::ok) {
			return status;
		}

		// FCHOICE_B stays 00, so the DLPF is used for both sensors.
		status = WriteVerified(regConfig, config.gyroDlpf & dlpfMask);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = WriteVerified(regSmplrtDiv, config.sampleRateDivider);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = WriteVerified(regGyroConfig, static_cast<uint8>(static_cast<uint8>(config.gyroRange) << 3));
		if (status != ResultStatus::ok) {
			return status;
		}

		status = WriteVerified(regAccelConfig, static_cast<uint8>(static_cast<uint8>(config.accelRange) << 3));
		if (status != ResultStatus::ok) {
			return status;
		}

		status = WriteVerified(regAccelConfig2, config.accelDlpf & dlpfMask);
		if (status != ResultStatus::ok) {
			return status;
		}

		accelLsbPerG = AccelLsbPerG(config.accelRange);
		gyroLsbPerDps = GyroLsbPerDps(config.gyroRange);
		return ResultStatus::ok;
	}


	// One burst read of the 14 data bytes from ACCEL_XOUT_H. Accelerometer in g, gyroscope in deg/s, temperature in deg C.
	Result<Sample> Read() {
		uint8 raw[burstBytes] = {};
		ResultStatus status = bus.ReadArray<uint8, uint8>(address, regAccelXoutH, raw, burstBytes);
		if (status != ResultStatus::ok) {
			return status;
		}

		Sample sample;
		Decode(raw, accelLsbPerG, gyroLsbPerDps, sample);
		return sample;
	}


	// Starts the FIFO with accelerometer, temperature and gyroscope, 14 bytes per frame at the sample rate. The INT pin
	// is not used: the caller drains the FIFO with ReadFifo on a timer.
	ResultStatus StartFifo() {
		ResultStatus status = WriteVerified(regFifoEn, fifoEnGyroAndAccel);
		if (status != ResultStatus::ok) {
			return status;
		}

		return ResetFifo();
	}


	// Reads the complete frames waiting in the FIFO, at most fifoMaxFrames per call. When the FIFO overflowed, the oldest
	// data was lost: the FIFO is reset, no frames are returned and overflowed is set. The caller logs that.
	ResultStatus ReadFifo(Sample* frames, uint32& frameCount, bool& overflowed) {
		frameCount = 0;
		overflowed = false;

		// Reading INT_STATUS clears the overflow flag, so the flag is taken here, once per call.
		Result<uint8> intStatus = bus.Read<uint8, uint8>(address, regIntStatus);
		if (intStatus.IsErr()) {
			return intStatus.Error();
		}
		if ((intStatus.Value() & intStatusFifoOverflow) != 0) {
			overflowed = true;
			return ResetFifo();
		}

		// FIFO_COUNTH latches both count bytes, so it is read first.
		Result<uint8> countHigh = bus.Read<uint8, uint8>(address, regFifoCountH);
		Result<uint8> countLow = bus.Read<uint8, uint8>(address, regFifoCountL);
		if (countHigh.IsErr()) {
			return countHigh.Error();
		}
		if (countLow.IsErr()) {
			return countLow.Error();
		}

		uint32 bytes = (static_cast<uint32>(countHigh.Value() & 0x0F) << 8) | countLow.Value();
		if (bytes > fifoCapacityBytes) {
			overflowed = true;
			return ResetFifo();
		}

		uint32 available = bytes / burstBytes;
		if (available > fifoMaxFrames) {
			available = fifoMaxFrames;
		}
		if (available == 0) {
			return ResultStatus::ok;
		}

		uint8 raw[fifoMaxFrames * burstBytes] = {};
		ResultStatus status = bus.ReadArray<uint8, uint8>(address, regFifoRw, raw, available * burstBytes);
		if (status != ResultStatus::ok) {
			return status;
		}

		for (uint32 index = 0; index < available; index++) {
			Decode(raw + index * burstBytes, accelLsbPerG, gyroLsbPerDps, frames[index]);
		}
		frameCount = available;
		return ResultStatus::ok;
	}


	// Averages count gyroscope samples taken intervalMs apart while the device is still. Returns busy, and no bias, when
	// the spread of any axis is larger than maxSpreadDps, because a moving device would bias the result.
	ResultStatus CalibrateGyroBias(uint32 count, uint32 intervalMs, float maxSpreadDps, GyroBias& bias) {
		if (count == 0) {
			return ResultStatus::invalidParameter;
		}

		GyroBias sum;
		GyroBias low;
		GyroBias high;
		for (uint32 index = 0; index < count; index++) {
			Result<Sample> read = Read();
			if (read.IsErr()) {
				return read.Error();
			}

			const Sample& sample = read.Value();
			sum.x = sum.x + sample.gyroX;
			sum.y = sum.y + sample.gyroY;
			sum.z = sum.z + sample.gyroZ;
			if (index == 0) {
				low = { sample.gyroX, sample.gyroY, sample.gyroZ };
				high = low;
			} else {
				low.x = sample.gyroX < low.x ? sample.gyroX : low.x;
				low.y = sample.gyroY < low.y ? sample.gyroY : low.y;
				low.z = sample.gyroZ < low.z ? sample.gyroZ : low.z;
				high.x = sample.gyroX > high.x ? sample.gyroX : high.x;
				high.y = sample.gyroY > high.y ? sample.gyroY : high.y;
				high.z = sample.gyroZ > high.z ? sample.gyroZ : high.z;
			}

			if (index + 1 < count) {
				System::DelayMs(intervalMs);
			}
		}

		bool still = (high.x - low.x) <= maxSpreadDps && (high.y - low.y) <= maxSpreadDps
			&& (high.z - low.z) <= maxSpreadDps;
		if (!still) {
			return ResultStatus::busy;
		}

		float divisor = static_cast<float>(count);
		bias.x = sum.x / divisor;
		bias.y = sum.y / divisor;
		bias.z = sum.z / divisor;
		return ResultStatus::ok;
	}


	// Converts the 14 bytes of the burst read (big-endian, in register order from 0x3B) to physical units. Public so the
	// host tests can check the scaling without a bus.
	static void Decode(const uint8* raw, float accelScale, float gyroScale, Sample& sample) {
		sample.accelX = static_cast<float>(ToInt16(raw, 0)) / accelScale;
		sample.accelY = static_cast<float>(ToInt16(raw, 2)) / accelScale;
		sample.accelZ = static_cast<float>(ToInt16(raw, 4)) / accelScale;
		sample.temperatureC = static_cast<float>(ToInt16(raw, 6)) / temperatureLsbPerC + temperatureOffsetC;
		sample.gyroX = static_cast<float>(ToInt16(raw, 8)) / gyroScale;
		sample.gyroY = static_cast<float>(ToInt16(raw, 10)) / gyroScale;
		sample.gyroZ = static_cast<float>(ToInt16(raw, 12)) / gyroScale;
	}


private:
	// Clears the FIFO and keeps it enabled (FIFO_RST is self-clearing).
	ResultStatus ResetFifo() {
		return bus.Write<uint8, uint8>(address, regUserCtrl, userCtrlFifoEnable | userCtrlFifoReset);
	}


	ResultStatus WriteVerified(uint8 reg, uint8 value) {
		ResultStatus status = bus.Write<uint8, uint8>(address, reg, value);
		if (status != ResultStatus::ok) {
			return status;
		}

		Result<uint8> readback = bus.Read<uint8, uint8>(address, reg);
		if (readback.IsErr()) {
			return readback.Error();
		}

		if (readback.Value() != value) {
			return ResultStatus::writeError;
		}
		return ResultStatus::ok;
	}


	static int16 ToInt16(const uint8* raw, uint32 index) {
		uint16 value = static_cast<uint16>((static_cast<uint16>(raw[index]) << 8) | raw[index + 1]);
		return static_cast<int16>(value);
	}
};
