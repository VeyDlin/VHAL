#pragma once
#include <VHAL.h>
#include <Drivers/Interface/USB/Class/DFU/DfuFunction.h>
#include <Drivers/Interface/USB/Class/DFU/DfuRuntimeFunction.h>
#include <Drivers/Interface/USB/Class/DFU/IDfuTarget.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuBuilder.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <Utilities/Result/Result.h>
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <vector>


// The DFU test devices: the descriptor sets of the state-machine tests, a fake target with scripted results, and fixtures that
// attach one device the way a host does. The expected bytes are not here; each test writes its own from the specification.
namespace DfuTest {


// Mode, download and upload, manifestation tolerant, detach by bus reset. The block buffer of the function is 64 bytes.
using ModeUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU mode")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanDnload()
				.CanUpload()
				.ManifestationTolerant()
				.DetachTimeoutMs(100)
				.TransferSize(64)
				.Name("Flash")));
}>;


// Download only, not manifestation tolerant and no WillDetach: the host must reset the device after manifestation.
using WaitResetUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU wait reset")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanDnload()
				.DetachTimeoutMs(100)
				.TransferSize(64)));
}>;


// Download only, not manifestation tolerant, WillDetach: the device detaches and attaches after manifestation.
using DetachUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU detach")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanDnload()
				.WillDetach()
				.DetachTimeoutMs(100)
				.TransferSize(64)));
}>;


// Upload only: DFU_DNLOAD is a stall.
using UploadOnlyUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU upload")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanUpload()
				.DetachTimeoutMs(100)
				.TransferSize(64)));
}>;


// Declares a transfer of 128 bytes, twice the function's block buffer: the device refuses to configure.
using BigTransferUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU big")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanDnload()
				.CanUpload()
				.DetachTimeoutMs(100)
				.TransferSize(128)));
}>;


// Run time: DFU_DETACH with a 255 ms detach timeout, no WillDetach.
using RuntimeUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0003)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU runtime")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Runtime()
				.DetachTimeoutMs(255)
				.TransferSize(64)));
}>;


// Run time with WillDetach: the device detaches itself on DFU_DETACH.
using RuntimeWillDetachUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0003)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU runtime detach")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Runtime()
				.WillDetach()
				.TransferSize(64)));
}>;


// Scripted target. Each result applies to every call of its operation. Writes and the image are recorded so a test can check
// what reached the target and in which order.
class FakeDfuTarget : public IDfuTarget {
public:
	struct WriteRecord {
		uint32 offset = 0;
		std::vector<uint8> data;
	};


	std::vector<WriteRecord> writes;
	std::vector<uint8> image;
	// When set, every Write() injects a bus reset first, as a reset that lands during a flash write does on the bus.
	SimUSBDeviceAdapter* resetOnWrite = nullptr;
	// Runs inside Write() and Manifest(), after the call is recorded: the host traffic that arrives while the flash is busy.
	std::function<void()> duringWrite;
	std::function<void()> duringManifest;
	uint32 beginCount = 0;
	uint8 lastAlternate = 0xFF;
	uint32 manifestCount = 0;
	DfuStatus beginResult = DfuStatus::OK;
	DfuStatus writeResult = DfuStatus::OK;
	DfuStatus manifestResult = DfuStatus::OK;
	bool readFails = false;
	bool complete = true;
	uint32 writePollMs = 15;
	uint32 manifestPollMs = 40;


	DfuStatus Begin(uint8 alternate) override {
		beginCount++;
		lastAlternate = alternate;
		return beginResult;
	}


	DfuStatus Write(uint32 offset, std::span<const uint8> data) override {
		WriteRecord record;
		record.offset = offset;
		record.data.assign(data.begin(), data.end());
		writes.push_back(record);
		if (resetOnWrite != nullptr) {
			resetOnWrite->InjectBusReset();
		}

		if (duringWrite) {
			duringWrite();
		}

		return writeResult;
	}


	Result<uint32> Read(uint32 offset, std::span<uint8> data) override {
		if (readFails) {
			return Result<uint32>(Err<ResultStatus>(ResultStatus::readError));
		}

		uint32 count = 0;
		while (count < data.size() && offset + count < image.size()) {
			data[count] = image[offset + count];
			count++;
		}

		return Result<uint32>(Ok<uint32>(count));
	}


	DfuStatus Manifest() override {
		manifestCount++;
		if (duringManifest) {
			duringManifest();
		}

		return manifestResult;
	}


	uint32 GetPollTimeoutMs(DfuOperation operation) const override {
		if (operation == DfuOperation::Write) {
			return writePollMs;
		}

		return manifestPollMs;
	}


	bool IsComplete() const override {
		return complete;
	}
};


// Resets the bus and configures the device, as a host does after it attaches.
inline void Enumerate(SimUSBDeviceAdapter& adapter) {
	adapter.InjectBusReset();
	SimHost::Control(adapter, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0));
	SimHost::Control(adapter, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0));
}


// Fixture of a mode device: the device, the fake target and the DFU function with a 64-byte block buffer.
template<typename UsbType>
struct ModeDevice {
	SimUSBDeviceAdapter sim;
	FakeDfuTarget target;
	DfuFunction<64> function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	ModeDevice()
		: function(UsbType::Function(0), target),
		  functions{ &function },
		  device(sim, UsbType::Set, functions) {
	}


	ResultStatus Start() {
		return device.Start(USBDeviceAdapter::Parameters());
	}


	// Attaches the device and configures it. The DFU function is the only function, so the configuration is its own.
	void Configure() {
		SIM_EXPECT_EQ(Start(), ResultStatus::ok);
		Enumerate(sim);
		SIM_EXPECT(device.GetState() == USBDeviceState::Configured);
	}
};


// Fixture of a run-time device.
template<typename UsbType>
struct RuntimeDevice {
	SimUSBDeviceAdapter sim;
	DfuRuntimeFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	RuntimeDevice()
		: function(UsbType::Function(0)),
		  functions{ &function },
		  device(sim, UsbType::Set, functions) {
	}


	ResultStatus Start() {
		return device.Start(USBDeviceAdapter::Parameters());
	}


	void Configure() {
		SIM_EXPECT_EQ(Start(), ResultStatus::ok);
		Enumerate(sim);
		SIM_EXPECT(device.GetState() == USBDeviceState::Configured);
	}
};


// Answer of an IN request: its status and the bytes the device sent.
struct Answer {
	ResultStatus status = ResultStatus::notReady;
	std::vector<uint8> data;
};


// Sends a class request to the interface (0x21) with a data stage of data. Returns the core's result for the request.
inline ResultStatus SendOut(SimUSBDeviceAdapter& sim, uint8 request, uint16 value, std::span<const uint8> data) {
	uint32 received = 0;
	return SimHost::Control(sim, SimHost::MakeSetup(0x21, request, value, 0, static_cast<uint16>(data.size())), data, std::span<uint8>(), received);
}


// Sends a class request with no data stage.
inline ResultStatus SendNoData(SimUSBDeviceAdapter& sim, uint8 request, uint16 value) {
	return SendOut(sim, request, value, std::span<const uint8>());
}


// Sends SET_CONFIGURATION with value (0 deconfigures the device, 1 configures it again).
inline ResultStatus SetConfiguration(SimUSBDeviceAdapter& sim, uint16 value) {
	return SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, value, 0x0000, 0));
}


// Sends a class request to the interface (0xA1) and reads its answer of up to length bytes.
inline Answer GetIn(SimUSBDeviceAdapter& sim, uint8 request, uint16 value, uint16 length) {
	std::array<uint8, 256> buffer{};
	uint32 received = 0;
	Answer answer;
	answer.status = SimHost::Control(sim, SimHost::MakeSetup(0xA1, request, value, 0, length), std::span<const uint8>(), buffer, received);
	answer.data.assign(buffer.begin(), buffer.begin() + received);
	return answer;
}


// The decoded DFU_GETSTATUS answer.
struct StatusAnswer {
	bool valid = false;
	DfuStatus status = DfuStatus::OK;
	uint32 pollTimeoutMs = 0;
	DfuState state = DfuState::dfuIDLE;
};


// Reads DFU_GETSTATUS and decodes its six bytes. valid is false when the request stalled or the answer is not six bytes.
inline StatusAnswer ReadStatus(SimUSBDeviceAdapter& sim) {
	Answer answer = GetIn(sim, static_cast<uint8>(DfuRequest::GetStatus), 0, 6);
	StatusAnswer decoded;
	if (answer.status != ResultStatus::ok || answer.data.size() != 6) {
		return decoded;
	}

	decoded.valid = true;
	decoded.status = static_cast<DfuStatus>(answer.data[0]);
	decoded.pollTimeoutMs = answer.data[1] | (answer.data[2] << 8) | (answer.data[3] << 16);
	decoded.state = static_cast<DfuState>(answer.data[4]);
	return decoded;
}


// Reads DFU_GETSTATE. Returns the state, or appIDLE with a stall result when the request stalled (see the stall tests).
inline DfuState ReadState(SimUSBDeviceAdapter& sim) {
	Answer answer = GetIn(sim, static_cast<uint8>(DfuRequest::GetState), 0, 1);
	if (answer.status != ResultStatus::ok || answer.data.size() != 1) {
		return DfuState::appIDLE;
	}

	return static_cast<DfuState>(answer.data[0]);
}


}
