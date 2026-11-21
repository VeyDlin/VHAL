#include <DemoUSBBench/DemoUSBBench.h>
#include <DemoUSBBench/Descriptors/BenchDescriptors.h>
#include <DemoUSBBench/Function/BenchFunction.h>
#include <Drivers/Interface/USB/Descriptor/USBSerialNumber.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>


// The SWD reader sees these counters. Once per second the main loop copies the function's snapshot into them, one field at a
// time, because a volatile struct cannot be assigned as a whole.
volatile BenchProtocol::Counters benchCounters __attribute__((used)) = {};


// The result of BSP::Init() for USART1, readable over SWD. ok means the console is working.
volatile ResultStatus benchConsoleStatus __attribute__((used)) = ResultStatus::ok;


namespace DemoUSBBench {
constexpr uint32 StatusIntervalMs = 1000;
// The Reconnect request is acknowledged with its status stage. The pull-up stays on for these few milliseconds, so the
// host takes that stage before the device disappears.
constexpr uint32 ReconnectSettleMs = 5;
static BenchFunction benchFunction(BenchUsb::Function(0));
static std::array<USBFunction*, 1> functions = { &benchFunction };
static USBDeviceAdapter::Parameters usbParameters;
static USBDevice usbDevice(BSP::usbAdapter, BenchUsb::Set, functions);


// The MCU's 96-bit unique ID. Its hex text is the serial number the host shows, so several boards of this firmware are told apart on
// one PC, and one board keeps the same serial number after a reboot. The descriptor is kept in static storage, as the device requires.
static std::array<uint8, 2 + 4 * BSP::UniqueIdSize> serialDescriptor{};


// Reads the unique ID and gives the device its serial number string. Called once, before the first Start(); the device keeps the span.
static ResultStatus SetUniqueSerialNumber() {
	std::array<uint8, BSP::UniqueIdSize> uniqueId{};
	BSP::ReadUniqueId(uniqueId);

	ResultStatus status = MakeUSBSerialNumberDescriptor(uniqueId, serialDescriptor);
	if (status != ResultStatus::ok) {
		return status;
	}

	return usbDevice.SetSerialNumber(serialDescriptor);
}


// The Reconnect request sets a flag in the interrupt. The disconnect happens here, in the main loop, because Stop() changes
// endpoint state and the interrupt must not do that. The pull-up is off for the requested time, then the device attaches again
// and the host enumerates it anew.
static void HandleReconnect() {
	uint16 delayMs = 0;
	if (!benchFunction.TakeReconnectRequest(delayMs)) {
		return;
	}

	System::DelayMs(ReconnectSettleMs);

	System::CriticalSection(true);
	usbDevice.Stop();
	System::CriticalSection(false);

	System::DelayMs(delayMs);

	ResultStatus status = usbDevice.Start(usbParameters);

	// The restarted device counts its adapter errors from zero, so the function's baseline starts again from zero.
	benchFunction.ResetUsbErrorBaseline();

	const char* result = "failed";
	if (status == ResultStatus::ok) {
		result = "ok";
	}

	System::console << "reconnect " << result << Console::endl;
}


static void PrintStatus() {
	benchFunction.SetUsbErrorTotal(usbDevice.GetErrorCount());

	BenchProtocol::Counters counters{};
	benchFunction.TakeCountersSnapshot(counters);

	benchCounters.bytesSent = counters.bytesSent;
	benchCounters.bytesReceived = counters.bytesReceived;
	benchCounters.receiveErrors = counters.receiveErrors;
	benchCounters.firstErrorOffset = counters.firstErrorOffset;
	benchCounters.busResets = counters.busResets;
	benchCounters.usbErrors = counters.usbErrors;

	const char* stateName = USBDeviceStateName(usbDevice.GetState());

	const char* modeName = "Unknown";
	switch (benchFunction.GetMode()) {
		case BenchProtocol::Mode::Idle: {
			modeName = "Idle";
			break;
		}

		case BenchProtocol::Mode::Source: {
			modeName = "Source";
			break;
		}

		case BenchProtocol::Mode::Sink: {
			modeName = "Sink";
			break;
		}

		case BenchProtocol::Mode::SourceAndSink: {
			modeName = "SourceAndSink";
			break;
		}

		case BenchProtocol::Mode::Loopback: {
			modeName = "Loopback";
			break;
		}
	}

	// The line goes to the console ring, which never waits for USART1. The field order and text are read by people on COM11; keep them as the README describes.
	System::console
		<< "t=" << System::GetMs()
		<< "ms state=" << stateName
		<< " addr=" << static_cast<uint32>(usbDevice.GetAddress())
		<< " mode=" << modeName
		<< " sent=" << counters.bytesSent
		<< " recv=" << counters.bytesReceived
		<< " rxErr=" << counters.receiveErrors
		<< " firstErr=" << counters.firstErrorOffset
		<< " busResets=" << counters.busResets
		<< " usbErr=" << counters.usbErrors
		<< Console::endl;
}


void Run(ResultStatus consoleStatus) {
	benchConsoleStatus = consoleStatus;

	if (SetUniqueSerialNumber() != ResultStatus::ok) {
		System::console << "USB serial number failed" << Console::endl;
	}

	ResultStatus status = usbDevice.Start(usbParameters);
	if (status == ResultStatus::ok) {
		System::console << "USB bench started" << Console::endl;
	} else {
		System::console << "USB start failed" << Console::endl;
	}

	uint64 nextStatusMs = System::GetMs() + StatusIntervalMs;
	while (true) {
		usbDevice.Process();
		HandleReconnect();

		if (System::GetMs() >= nextStatusMs) {
			nextStatusMs += StatusIntervalMs;
			PrintStatus();
		}
	}
}
}
