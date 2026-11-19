#pragma once
#include <VHAL.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include "FixedList.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>


// A small bulk function for the host tests. On configuration it opens a bulk IN and a bulk OUT endpoint and keeps the OUT
// endpoint armed, as a real function does. It answers three class requests on its interface and records every callback,
// so a test can check what the core routed to it and when.
//
// The callbacks run in interrupt context, so the records are fixed lists and never allocate. A record that does not fit is
// dropped and sets its list's overflowed flag; AnyOverflow() reports it, and the tests check it.
class TestFunction : public USBFunction {
public:
	struct Completion {
		uint8 address = 0;
		uint32 byteCount = 0;
	};


	struct HaltChange {
		uint8 address = 0;
		bool halted = false;
	};


	// The class requests the function answers. The core passes the bRequest through.
	static constexpr uint8 ClassInRequest = 0x10;
	static constexpr uint8 ClassOutRequest = 0x20;
	static constexpr uint8 ClassNoDataRequest = 0x30;
	// Not answered: the function returns notSupported, so the core stalls.
	static constexpr uint8 UnhandledRequest = 0x40;
	// Capacities of the records. Each test stays well below them.
	static constexpr std::size_t MaxRequests = 16;
	static constexpr std::size_t MaxControlData = 16;
	static constexpr std::size_t MaxReceivedOut = 64;
	static constexpr std::size_t MaxCompletions = 32;
	static constexpr std::size_t MaxHaltChanges = 8;
	static constexpr std::size_t MaxQueuedIn = 64;
	uint32 configureCount = 0;
	uint32 deconfigureCount = 0;
	uint32 busResetCount = 0;
	uint32 suspendCount = 0;
	uint32 resumeCount = 0;
	bool failConfigure = false;
	FixedList<uint8, MaxRequests> requests;
	FixedList<uint8, MaxControlData> controlOutData;
	FixedList<uint8, MaxReceivedOut> receivedOut;
	FixedList<Completion, MaxCompletions> completions;
	FixedList<HaltChange, MaxHaltChanges> haltChanges;
	std::array<uint8, 4> classInResponse = { 0xDE, 0xAD, 0xBE, 0xEF };
	std::array<uint8, 64> outBuffer = {};
	std::array<uint8, 16> controlOutBuffer = {};


private:
	std::array<uint8, MaxQueuedIn> queuedIn = {};
	std::size_t queuedInCount = 0;
	uint8 inAddress;
	uint8 outAddress;
	bool inQueued = false;
	bool inArmed = false;
	bool inHalted = false;


public:
	TestFunction(std::span<const uint8> interfaces, std::span<const uint8> endpoints, uint8 bulkInAddress, uint8 bulkOutAddress)
		: USBFunction(interfaces, endpoints),
		  inAddress(bulkInAddress),
		  outAddress(bulkOutAddress) {
	}


	// Queues data for the bulk IN endpoint. The data is copied, and it is armed by the next Process(), so the test controls when
	// it starts. Data past MaxQueuedIn is cut; the tests queue less.
	void QueueIn(std::span<const uint8> data) {
		queuedInCount = std::min(data.size(), MaxQueuedIn);
		std::copy_n(data.begin(), queuedInCount, queuedIn.begin());
		inQueued = true;
	}


	bool IsInArmed() const {
		return inArmed;
	}


	bool AnyOverflow() const {
		return requests.overflowed || controlOutData.overflowed || receivedOut.overflowed || completions.overflowed || haltChanges.overflowed;
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		configureCount++;

		if (failConfigure) {
			return ResultStatus::notSupported;
		}

		ResultStatus status = context.OpenEndpoint(MakeBulk(inAddress));

		if (status != ResultStatus::ok) {
			return status;
		}

		status = context.OpenEndpoint(MakeBulk(outAddress));

		if (status != ResultStatus::ok) {
			return status;
		}

		return context.StartOutTransfer(outAddress, outBuffer);
	}


	void OnDeconfigured() override {
		deconfigureCount++;
		inQueued = false;
		inArmed = false;
	}


	void OnBusReset() override {
		busResetCount++;
		inQueued = false;
		inArmed = false;
		inHalted = false;
	}


	void OnSuspend() override {
		suspendCount++;
	}


	void OnResume() override {
		resumeCount++;
	}


	ResultStatus OnControlRequest(const USBSetupPacket& setup, USBDeviceContext& context) override {
		requests.Push(setup.request);

		if (setup.request == ClassInRequest) {
			return context.ControlSendData(classInResponse);
		}

		if (setup.request == ClassOutRequest) {
			return context.ControlReceiveData(controlOutBuffer);
		}

		if (setup.request == ClassNoDataRequest) {
			return ResultStatus::ok;
		}

		return ResultStatus::notSupported;
	}


	ResultStatus OnControlDataReceived(const USBSetupPacket&, std::span<const uint8> data, USBDeviceContext&) override {
		controlOutData.Clear();

		for (uint8 value : data) {
			controlOutData.Push(value);
		}

		return ResultStatus::ok;
	}


	// A 0-byte OUT completion is no data, and the function re-arms the buffer the same way as a full one.
	void OnTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) override {
		completions.Push(Completion{ address, byteCount });

		if (address == inAddress) {
			inArmed = false;
			inQueued = false;
		}

		if (address == outAddress) {
			for (uint32 index = 0; index < byteCount && index < outBuffer.size(); index++) {
				receivedOut.Push(outBuffer[index]);
			}

			context.StartOutTransfer(outAddress, outBuffer);
		}
	}


	// A halt drops the armed IN transfer, and the queued data stays queued. A clear re-arms it, as a real function does.
	void OnEndpointHaltChanged(uint8 address, bool halted, USBDeviceContext& context) override {
		haltChanges.Push(HaltChange{ address, halted });

		if (address != inAddress) {
			return;
		}

		inHalted = halted;

		if (halted) {
			inArmed = false;
			return;
		}

		if (inQueued) {
			inArmed = context.StartInTransfer(inAddress, QueuedIn(), false) == ResultStatus::ok;
		}
	}


	void Process(USBDeviceContext& context) override {
		if (!inQueued || inArmed || inHalted) {
			return;
		}

		inArmed = context.StartInTransfer(inAddress, QueuedIn(), false) == ResultStatus::ok;
	}


private:
	std::span<const uint8> QueuedIn() const {
		return std::span<const uint8>(queuedIn.data(), queuedInCount);
	}


	static USBDeviceAdapter::EndpointConfig MakeBulk(uint8 address) {
		USBDeviceAdapter::EndpointConfig config;
		config.address = address;
		config.type = USBEndpointType::Bulk;
		config.maxPacketSize = 64;
		config.doubleBuffer = false;
		return config;
	}
};
