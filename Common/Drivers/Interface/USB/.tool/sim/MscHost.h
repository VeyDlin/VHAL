#pragma once
#include <VHAL.h>
#include <Drivers/Interface/USB/Class/MSC/MscFunction.h>
#include <Drivers/Interface/USB/Class/MSC/MscProtocol.h>
#include <Drivers/Interface/USB/Class/MSC/USBMscBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <Drivers/Memory/BlockDevice/RamBlockDevice.h>
#include <Utilities/FileSystem/Fat12ImageBuilder.h>
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <array>
#include <cstddef>
#include <span>


// The mass storage host model of the tests: a Bulk-Only host that sends CBWs, moves the data stages and reads CSWs the way the
// BOT host requirements describe (BOT 1.0 section 6.7). Each NAK runs the device's Process(), as the application's loop would, so
// the medium work completes while the host waits. A STALL is cleared by the host with CLEAR_FEATURE(ENDPOINT_HALT), which runs
// through the device core, so the function's halt callback is part of what a test checks.
//
// Also here: the device fixture (one function on the MSC descriptors, attached the way a host attaches it) and the identity and
// the descriptor set the tests share.
namespace MscTest {


// The identity every test device reports.
inline constexpr MscIdentity Identity{ "VHAL", "RAM Disk", "1.00", true };


inline constexpr uint8 InterfaceNumber = 0;
inline constexpr uint8 InAddress = 0x81;
inline constexpr uint8 OutAddress = 0x02;
inline constexpr uint8 ClassFeatureEndpointHalt = 0;


// The device descriptors of the mass storage test device: pid.codes test identity, one MSC interface with 0x81 and 0x02.
using MscUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Mass storage")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMscBuilder().Name("Mass storage").BulkIn(InAddress).BulkOut(OutAddress)));
}>;


// The command status wrapper as the host read it.
struct Csw {
	bool valid = false;
	uint32 tag = 0;
	uint32 residue = 0;
	uint8 status = 0xFF;
};


// What one command did on the bus, as the host saw it.
struct Outcome {
	// ok when the CBW was taken. accessError when the device stalled the OUT pipe before it, which a test checks for an invalid CBW.
	ResultStatus cbw = ResultStatus::ok;
	// Bytes the host received in the data stage (device to host), and sent in it (host to device).
	uint32 dataIn = 0;
	uint32 dataOut = 0;
	// The host met a STALL in the data stage, and cleared the halt.
	bool inStalled = false;
	bool outStalled = false;
	Csw csw;
	// ok when the CSW was read and is valid.
	ResultStatus cswStatus = ResultStatus::notReady;
};


// The attempts a host makes on NAK before it gives up. The device's Process() runs on each one.
inline constexpr uint32 MaxNakAttempts = 64;


class BotHost {
private:
	SimUSBDeviceAdapter& adapter;
	USBDevice& device;
	uint32 nextTag = 1;


public:
	BotHost(SimUSBDeviceAdapter& usbAdapter, USBDevice& usbDevice)
		: adapter(usbAdapter),
		  device(usbDevice) {
	}


	// Builds the 31-byte CBW: the signature, the tag, the length, the direction, LUN 0 and the command block.
	static std::array<uint8, MscCbwLength> MakeCbw(uint32 tag, uint32 length, bool dataIn, std::span<const uint8> block) {
		std::array<uint8, MscCbwLength> cbw{};
		Put32(cbw, 0, MscCbwSignature);
		Put32(cbw, 4, tag);
		Put32(cbw, 8, length);
		cbw[12] = dataIn ? MscFlagDataIn : 0x00;
		cbw[13] = 0x00;
		cbw[14] = static_cast<uint8>(block.size());
		for (std::size_t index = 0; index < block.size() && index < 16; index++) {
			cbw[15 + index] = block[index];
		}

		return cbw;
	}


	uint32 NextTag() {
		return nextTag++;
	}


	// Runs one command with the given tag. The CBW's length is length, its direction is dataIn, and the data stage moves through out (for
	// host-to-device data) or in (for device-to-host data, where in must hold the whole length).
	Outcome Command(uint32 tag, std::span<const uint8> block, uint32 length, bool dataIn, std::span<const uint8> out, std::span<uint8> in) {
		Outcome outcome;
		std::array<uint8, MscCbwLength> cbw = MakeCbw(tag, length, dataIn, block);
		outcome.cbw = SendRaw(cbw);
		if (outcome.cbw != ResultStatus::ok) {
			return outcome;
		}

		if (length > 0 && dataIn) {
			ReadData(in, length, outcome);
		} else if (length > 0) {
			WriteData(out, length, outcome);
		}

		if (outcome.inStalled) {
			ClearHalt(InAddress);
		}

		if (outcome.outStalled) {
			ClearHalt(OutAddress);
		}

		outcome.cswStatus = ReadCsw(tag, outcome.csw);
		return outcome;
	}


	// Runs a command with a fresh tag. The data helpers take the same arguments as Command().
	Outcome Run(std::span<const uint8> block, uint32 length, bool dataIn, std::span<const uint8> out, std::span<uint8> in) {
		return Command(NextTag(), block, length, dataIn, out, in);
	}


	// Sends raw bytes as the CBW, and reads the CSW that answers it. The CSW must echo the tag the bytes carry (bytes 4 to 7), when they
	// have one. A test uses it for a CBW that is not valid, or one the test builds by hand.
	Outcome RawCbw(std::span<const uint8> bytes) {
		Outcome outcome;
		outcome.cbw = SendRaw(bytes);
		if (outcome.cbw == ResultStatus::ok) {
			uint32 tag = bytes.size() >= 8 ? Get32(bytes, 4) : 0;
			outcome.cswStatus = ReadCsw(tag, outcome.csw);
		}

		return outcome;
	}


	// Bulk-Only Mass Storage Reset: a class request on the interface, with no data.
	ResultStatus Reset() {
		return SimHost::Control(adapter, SimHost::MakeSetup(0x21, MscRequestBulkOnlyReset, 0, InterfaceNumber, 0));
	}


	// Clears the halt of an endpoint, as the host does in the reset recovery and after a stall.
	ResultStatus ClearHalt(uint8 address) {
		return SimHost::Control(adapter, SimHost::MakeSetup(0x02, 1, ClassFeatureEndpointHalt, address, 0));
	}


	// Get Max LUN: one byte. Returns the status and the byte.
	ResultStatus GetMaxLun(uint8& lun) {
		std::array<uint8, 1> answer{ 0xFF };
		uint32 received = 0;
		ResultStatus status = SimHost::Control(adapter, SimHost::MakeSetup(0xA1, MscRequestGetMaxLun, 0, InterfaceNumber, 1), std::span<const uint8>(), answer, received);
		lun = answer[0];
		if (status == ResultStatus::ok && received != 1) {
			return ResultStatus::invalidData;
		}

		return status;
	}


private:
	static void Put32(std::span<uint8> bytes, std::size_t offset, uint32 value) {
		for (std::size_t index = 0; index < 4; index++) {
			bytes[offset + index] = static_cast<uint8>((value >> (8 * index)) & 0xFF);
		}
	}


	static uint32 Get32(std::span<const uint8> bytes, std::size_t offset) {
		return static_cast<uint32>(bytes[offset]) | (static_cast<uint32>(bytes[offset + 1]) << 8) | (static_cast<uint32>(bytes[offset + 2]) << 16) | (static_cast<uint32>(bytes[offset + 3]) << 24);
	}


	// Sends one OUT packet sequence for the CBW. A NAK runs Process(). A STALL is returned as accessError.
	ResultStatus SendRaw(std::span<const uint8> bytes) {
		for (uint32 attempt = 0; attempt < MaxNakAttempts; attempt++) {
			Result<uint32> stored = adapter.HostOut(OutAddress, bytes);
			if (stored.IsOk()) {
				return ResultStatus::ok;
			}

			if (stored.Error() != ResultStatus::notReady) {
				return stored.Error();
			}

			device.Process();
		}

		return ResultStatus::notReady;
	}


	// Host-to-device data: sends the length bytes of out, one armed transfer at a time.
	void WriteData(std::span<const uint8> out, uint32 length, Outcome& outcome) {
		while (outcome.dataOut < length) {
			Result<uint32> stored = HostOutWithNak(out.subspan(outcome.dataOut));
			if (!stored.IsOk()) {
				if (stored.Error() == ResultStatus::accessError) {
					outcome.outStalled = true;
				}

				return;
			}

			outcome.dataOut += stored.Value();
		}
	}


	// Device-to-host data: takes armed transfers until the length is in, or the device stalls the IN pipe.
	void ReadData(std::span<uint8> in, uint32 length, Outcome& outcome) {
		while (outcome.dataIn < length) {
			uint32 count = 0;
			ResultStatus status = HostInWithNak(in.subspan(outcome.dataIn), count);
			if (status == ResultStatus::accessError) {
				outcome.inStalled = true;
				return;
			}

			if (status != ResultStatus::ok) {
				return;
			}

			outcome.dataIn += count;
		}
	}


	Result<uint32> HostOutWithNak(std::span<const uint8> data) {
		for (uint32 attempt = 0; attempt < MaxNakAttempts; attempt++) {
			Result<uint32> stored = adapter.HostOut(OutAddress, data);
			if (stored.IsOk() || stored.Error() != ResultStatus::notReady) {
				return stored;
			}

			device.Process();
		}

		return ResultStatus::notReady;
	}


	ResultStatus HostInWithNak(std::span<uint8> out, uint32& count) {
		for (uint32 attempt = 0; attempt < MaxNakAttempts; attempt++) {
			ResultStatus status = adapter.HostIn(InAddress, out, count);
			if (status != ResultStatus::notReady) {
				return status;
			}

			device.Process();
		}

		return ResultStatus::notReady;
	}


	// Reads the CSW. A STALL clears the IN halt and asks again, as the host does; a NAK runs Process().
	ResultStatus ReadCsw(uint32 tag, Csw& csw) {
		std::array<uint8, MscCswLength> bytes{};
		uint32 count = 0;
		ResultStatus status = ResultStatus::notReady;

		for (uint32 attempt = 0; attempt < 4; attempt++) {
			status = HostInWithNak(bytes, count);
			if (status != ResultStatus::accessError) {
				break;
			}

			ClearHalt(InAddress);
		}

		if (status != ResultStatus::ok || count != MscCswLength) {
			return ResultStatus::notReady;
		}

		csw.tag = Get32(bytes, 4);
		csw.residue = Get32(bytes, 8);
		csw.status = bytes[12];
		csw.valid = Get32(bytes, 0) == MscCswSignature && csw.tag == tag;
		return csw.valid ? ResultStatus::ok : ResultStatus::invalidData;
	}
};


// The bytes of the 31-byte CBW as a test writes them out, for the CBW tests. It uses the same field layout as the BOT table 5.1.
inline std::array<uint8, MscCbwLength> HandCbw(uint32 tag, uint32 length, uint8 flags, uint8 lun, uint8 commandLength, std::span<const uint8> block) {
	std::array<uint8, MscCbwLength> cbw{};
	cbw[0] = 0x55;
	cbw[1] = 0x53;
	cbw[2] = 0x42;
	cbw[3] = 0x43;
	cbw[4] = static_cast<uint8>(tag & 0xFF);
	cbw[5] = static_cast<uint8>((tag >> 8) & 0xFF);
	cbw[6] = static_cast<uint8>((tag >> 16) & 0xFF);
	cbw[7] = static_cast<uint8>((tag >> 24) & 0xFF);
	cbw[8] = static_cast<uint8>(length & 0xFF);
	cbw[9] = static_cast<uint8>((length >> 8) & 0xFF);
	cbw[10] = static_cast<uint8>((length >> 16) & 0xFF);
	cbw[11] = static_cast<uint8>((length >> 24) & 0xFF);
	cbw[12] = flags;
	cbw[13] = lun;
	cbw[14] = commandLength;
	for (std::size_t index = 0; index < block.size() && index < 16; index++) {
		cbw[15 + index] = block[index];
	}

	return cbw;
}


// One device: the adapter, a disk of type TDisk holding image, one MSC function with a BufferSize data buffer, and the device core.
// Attach() starts it and enumerates it the way Windows does, so the configuration is set when it returns.
template<std::size_t BufferSize, typename TDisk>
struct MscDevice {
	SimUSBDeviceAdapter sim;
	TDisk disk;
	MscFunction<BufferSize> function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	explicit MscDevice(std::span<const uint8> image = std::span<const uint8>(), const MscIdentity& deviceIdentity = Identity)
		: disk(image),
		  function(MscUsb::Function(0), disk, deviceIdentity),
		  functions{ &function },
		  device(sim, MscUsb::Set, functions) {
	}


	// Start, bus reset, the device descriptor, SET_ADDRESS, the configuration and SET_CONFIGURATION. Returns the status of the last step
	// that failed, or ok.
	ResultStatus Attach() {
		ResultStatus status = device.Start(USBDeviceAdapter::Parameters());
		if (status != ResultStatus::ok) {
			return status;
		}

		sim.InjectBusReset();
		std::array<uint8, 18> descriptor{};
		uint32 received = 0;
		status = SimHost::Control(sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 18), std::span<const uint8>(), descriptor, received);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, 1, 0x0000, 0));
		if (status != ResultStatus::ok) {
			return status;
		}

		return SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0));
	}
};


// The usual device: a RAM disk of BlockCount blocks.
template<std::size_t BufferSize, uint32 BlockCount>
using RamMscDevice = MscDevice<BufferSize, RamBlockDevice<512, BlockCount>>;


// A 64 KiB volume with a label and one small file, mounted at once.
using SmallVolume = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(128)
		.VolumeLabel("VHAL DISK")
		.File("readme.txt", "Hello from the RAM disk.\r\n");
}>;


}
