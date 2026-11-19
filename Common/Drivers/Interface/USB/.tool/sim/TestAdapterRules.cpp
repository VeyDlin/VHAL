#include <VHAL.h>
#include "FixedList.h"
#include "SimAssert.h"
#include "SimUSBDeviceAdapter.h"
#include <array>
#include <cstddef>
#include <string_view>

// Checks the simulated controller against the USBDeviceAdapter contract, without the device core. The core tests depend on
// these rules, so a wrong controller would fail them in a misleading way.


namespace {


// The events the adapter raised, in order. The callbacks write into fixed lists, so they never allocate.
struct EventLog {
	FixedList<uint8, 64> completionAddresses;
	FixedList<uint32, 64> completionCounts;
	FixedList<SimUSBDeviceAdapter::Error, 16> errors;
	// E for an error, C for a completion, in the order they were raised.
	FixedList<char, 64> order;
	// The device address when the latest completion ran. A pending SET_ADDRESS must already be applied by then.
	uint8 addressAtCompletion = 0;
	uint32 busResets = 0;
	uint32 setups = 0;
	uint32 resumes = 0;
	uint32 frames = 0;
	uint16 lastFrame = 0;


	bool AnyOverflow() const {
		return completionAddresses.overflowed || completionCounts.overflowed || errors.overflowed || order.overflowed;
	}
};


bool OrderIs(const EventLog& log, std::string_view expected) {
	return std::string_view(log.order.items.data(), log.order.count) == expected;
}


void Attach(SimUSBDeviceAdapter& sim, EventLog& log) {
	sim.onBusReset = [&log](USBSpeed) {
		log.busResets++;
	};

	sim.onSetup = [&log](const USBSetupBytes&) {
		log.setups++;
	};

	sim.onTransferComplete = [&sim, &log](uint8 address, uint32 byteCount) {
		log.completionAddresses.Push(address);
		log.completionCounts.Push(byteCount);
		log.order.Push('C');
		log.addressAtCompletion = sim.GetAddress();
	};

	sim.onError = [&log](SimUSBDeviceAdapter::Error error) {
		log.errors.Push(error);
		log.order.Push('E');
	};

	sim.onResume = [&log]() {
		log.resumes++;
	};

	sim.onStartOfFrame = [&log](uint16 frameNumber) {
		log.frames++;
		log.lastFrame = frameNumber;
	};
}


USBDeviceAdapter::EndpointConfig Bulk(uint8 address, uint16 maxPacketSize) {
	USBDeviceAdapter::EndpointConfig config;
	config.address = address;
	config.type = USBEndpointType::Bulk;
	config.maxPacketSize = maxPacketSize;
	config.doubleBuffer = false;
	return config;
}


USBDeviceAdapter::EndpointConfig DoubleBulk(uint8 address, uint16 maxPacketSize) {
	USBDeviceAdapter::EndpointConfig config = Bulk(address, maxPacketSize);
	config.doubleBuffer = true;
	return config;
}


// Brings the controller to a connected, reset state with the two endpoints the tests use (0x81 IN and 0x01 OUT).
void Prepare(SimUSBDeviceAdapter& sim) {
	USBDeviceAdapter::Parameters parameters;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	sim.InjectBusReset();
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 64)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x01, 64)), ResultStatus::ok);
}


uint16 MaxPacketSize(const SimUSBDeviceAdapter& sim, uint8 address) {
	const USBDeviceAdapter::EndpointConfig* config = sim.FindEndpoint(address);
	return config != nullptr ? config->maxPacketSize : 0;
}


void TestParameters() {
	SimUSBDeviceAdapter sim;
	USBDeviceAdapter::Parameters parameters;
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::notReady);

	parameters.endpointCount = 0;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::notReady);

	parameters.endpointCount = SimUSBDeviceAdapter::HardwareEndpointCount + 1;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::invalidParameter);

	parameters.endpointCount = SimUSBDeviceAdapter::HardwareEndpointCount;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.GetParameters().endpointCount, SimUSBDeviceAdapter::HardwareEndpointCount);

	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	SIM_EXPECT(sim.IsConnected());
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::busy);
}


void TestEndpointZeroAndBusReset() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	USBDeviceAdapter::Parameters parameters;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);

	SIM_EXPECT(sim.FindEndpoint(0x80) == nullptr);
	SIM_EXPECT_EQ(sim.StartInTransfer(0x80, std::span<const uint8>(), false), ResultStatus::notExist);

	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 64)), ResultStatus::ok);
	sim.InjectBusReset(USBSpeed::Full);

	SIM_EXPECT_EQ(log.busResets, 1);
	SIM_EXPECT_EQ(sim.GetAddress(), 0);
	SIM_EXPECT(sim.FindEndpoint(0x81) == nullptr);
	SIM_EXPECT_EQ(MaxPacketSize(sim, 0x80), 64);
	SIM_EXPECT_EQ(MaxPacketSize(sim, 0x00), 64);
	SIM_EXPECT(sim.FindEndpoint(0x80)->type == USBEndpointType::Control);

	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 64)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, std::span<const uint8>(), false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.StartInTransfer(0x80, std::span<const uint8>(), false), ResultStatus::ok);
	sim.InjectBusReset();
	SIM_EXPECT(!sim.HasArmedTransfer(0x80));
	SIM_EXPECT(!sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(log.completionCounts.count, 0);
}


void TestOpenRules() {
	SimUSBDeviceAdapter sim;
	USBDeviceAdapter::Parameters parameters;
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 64)), ResultStatus::notReady);
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	sim.InjectBusReset();

	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x80, 64)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x00, 64)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x88, 64)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x91, 64)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 10)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 0)), ResultStatus::invalidParameter);

	USBDeviceAdapter::EndpointConfig isochronous = Bulk(0x81, 64);
	isochronous.type = USBEndpointType::Isochronous;
	SIM_EXPECT_EQ(sim.OpenEndpoint(isochronous), ResultStatus::invalidParameter);

	USBDeviceAdapter::EndpointConfig interrupt = Bulk(0x82, 0);
	interrupt.type = USBEndpointType::Interrupt;
	SIM_EXPECT_EQ(sim.OpenEndpoint(interrupt), ResultStatus::invalidParameter);
	interrupt.maxPacketSize = 65;
	SIM_EXPECT_EQ(sim.OpenEndpoint(interrupt), ResultStatus::invalidParameter);
	interrupt.maxPacketSize = 64;
	SIM_EXPECT_EQ(sim.OpenEndpoint(interrupt), ResultStatus::ok);

	USBDeviceAdapter::EndpointConfig doubleInterrupt = Bulk(0x83, 8);
	doubleInterrupt.type = USBEndpointType::Interrupt;
	doubleInterrupt.doubleBuffer = true;
	SIM_EXPECT_EQ(sim.OpenEndpoint(doubleInterrupt), ResultStatus::invalidParameter);

	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 64)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 32)), ResultStatus::ok);
	SIM_EXPECT_EQ(MaxPacketSize(sim, 0x81), 32);
}


// The fsdev rule for the two directions of one number, which share an endpoint register (see SimUSBDeviceAdapter::OpenEndpoint).
// Double buffering needs the number to itself. A single open beside a double-buffered direction is a conflict the caller made.
void TestSharedNumberRules() {
	SimUSBDeviceAdapter sim;
	USBDeviceAdapter::Parameters parameters;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	sim.InjectBusReset();

	// Both directions single-buffered: the pair opens. A double request for either one is refused as a capability, and the
	// refusal leaves the open endpoints as they were.
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 64)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x01, 64)), ResultStatus::ok);
	SIM_EXPECT(!sim.IsDoubleBuffered(0x81));
	SIM_EXPECT(!sim.IsDoubleBuffered(0x01));
	SIM_EXPECT_EQ(sim.OpenEndpoint(DoubleBulk(0x81, 64)), ResultStatus::notSupported);
	SIM_EXPECT_EQ(sim.OpenEndpoint(DoubleBulk(0x01, 64)), ResultStatus::notSupported);
	SIM_EXPECT(!sim.IsDoubleBuffered(0x81));
	SIM_EXPECT(!sim.IsDoubleBuffered(0x01));
	SIM_EXPECT(sim.FindEndpoint(0x81) != nullptr);
	SIM_EXPECT(sim.FindEndpoint(0x01) != nullptr);

	// The type bits are shared as well: an interrupt OUT beside a bulk IN is a mismatch.
	USBDeviceAdapter::EndpointConfig interruptOut = Bulk(0x01, 8);
	interruptOut.type = USBEndpointType::Interrupt;
	SIM_EXPECT_EQ(sim.OpenEndpoint(interruptOut), ResultStatus::invalidParameter);
	SIM_EXPECT(!sim.IsDoubleBuffered(0x01));

	// Closing the OUT direction frees the number, so the IN can take double buffering. Then the OUT is a conflict again.
	SIM_EXPECT_EQ(sim.CloseEndpoint(0x01), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(DoubleBulk(0x81, 64)), ResultStatus::ok);
	SIM_EXPECT(sim.IsDoubleBuffered(0x81));
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x01, 64)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.OpenEndpoint(DoubleBulk(0x01, 64)), ResultStatus::notSupported);
	SIM_EXPECT(sim.FindEndpoint(0x01) == nullptr);
	SIM_EXPECT(sim.IsDoubleBuffered(0x81));

	// Another number is independent: a double-buffered OUT on 0x02 is granted beside the double-buffered 0x81.
	SIM_EXPECT_EQ(sim.OpenEndpoint(DoubleBulk(0x02, 64)), ResultStatus::ok);
	SIM_EXPECT(sim.IsDoubleBuffered(0x02));
	SIM_EXPECT(sim.IsDoubleBuffered(0x81));

	// A single IN reopen on a double-buffered number drops the double buffering when nothing else holds the number.
	SIM_EXPECT_EQ(sim.CloseEndpoint(0x02), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 64)), ResultStatus::ok);
	SIM_EXPECT(!sim.IsDoubleBuffered(0x81));
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x01, 64)), ResultStatus::ok);
	SIM_EXPECT(!sim.IsDoubleBuffered(0x01));
}


void TestPacketMemory() {
	SimUSBDeviceAdapter sim;
	USBDeviceAdapter::Parameters parameters;
	parameters.endpointCount = SimUSBDeviceAdapter::HardwareEndpointCount;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	sim.InjectBusReset();

	// Endpoint 0 uses 128 bytes of the 1024. Seven double-buffered 64-byte IN endpoints fill the rest exactly.
	for (uint8 number = 1; number <= 7; number++) {
		USBDeviceAdapter::EndpointConfig config = Bulk(USBEndpoint::MakeIn(number), 64);
		config.doubleBuffer = true;
		SIM_EXPECT_EQ(sim.OpenEndpoint(config), ResultStatus::ok);
	}

	// The OUT on number 1 shares its register with the double-buffered IN, so the shared-number rule refuses it before packet
	// memory is looked at. With every number in use and 64-byte packets, the simulator cannot reach outOfMemory at all.
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x01, 64)), ResultStatus::invalidParameter);

	SIM_EXPECT_EQ(sim.CloseEndpoint(0x81), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x01, 64)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.CloseEndpoint(0x81), ResultStatus::notExist);
	SIM_EXPECT_EQ(sim.CloseEndpoint(0x00), ResultStatus::invalidParameter);
}


void TestTransferRules() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	Prepare(sim);

	std::array<uint8, 64> inPayload = {};
	std::array<uint8, 64> outBuffer = {};
	std::array<uint8, 256> host = {};
	uint32 hostCount = 0;

	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::notReady);
	SIM_EXPECT_EQ(sim.HostIn(0x01, host, hostCount), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.HostIn(0x82, host, hostCount), ResultStatus::notExist);
	SIM_EXPECT_EQ(sim.StartInTransfer(0x01, inPayload, false), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.StartOutTransfer(0x81, outBuffer), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.StartInTransfer(0x82, inPayload, false), ResultStatus::notExist);

	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, inPayload, false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, inPayload, false), ResultStatus::busy);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 64)), ResultStatus::busy);
	SIM_EXPECT_EQ(sim.StartOutTransfer(0x01, outBuffer), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.StartOutTransfer(0x01, outBuffer), ResultStatus::busy);

	sim.Disconnect();
	SIM_EXPECT(!sim.HasArmedTransfer(0x81));
	SIM_EXPECT(!sim.HasArmedTransfer(0x01));
	SIM_EXPECT_EQ(log.completionCounts.count, 0);
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, inPayload, false), ResultStatus::notReady);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, inPayload, false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::ok);
	SIM_EXPECT_EQ(hostCount, 64);
	SIM_EXPECT_EQ(log.completionCounts.count, 1);
}


void TestZeroLengthPacketRule() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	Prepare(sim);

	std::array<uint8, 128> payload = {};

	for (std::size_t index = 0; index < payload.size(); index++) {
		payload[index] = static_cast<uint8>(index);
	}

	// Each toggle flip is one packet on the wire, a zero-length packet included.
	std::array<uint8, 256> host = {};
	uint32 hostCount = 0;
	bool before = sim.GetDataToggle(0x81);

	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, std::span<const uint8>(payload).first(128), false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::ok);
	SIM_EXPECT_EQ(hostCount, 128);
	SIM_EXPECT_EQ(log.completionCounts.Back(), 128);
	SIM_EXPECT(sim.GetDataToggle(0x81) == before);

	before = sim.GetDataToggle(0x81);
	hostCount = 0;
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, std::span<const uint8>(payload).first(128), true), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::ok);
	SIM_EXPECT_EQ(hostCount, 128);
	SIM_EXPECT_EQ(log.completionCounts.Back(), 128);
	SIM_EXPECT(sim.GetDataToggle(0x81) != before);

	before = sim.GetDataToggle(0x81);
	hostCount = 0;
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, std::span<const uint8>(), false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::ok);
	SIM_EXPECT_EQ(hostCount, 0);
	SIM_EXPECT_EQ(log.completionCounts.Back(), 0);
	SIM_EXPECT(sim.GetDataToggle(0x81) != before);

	before = sim.GetDataToggle(0x81);
	hostCount = 0;
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, std::span<const uint8>(payload).first(10), true), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::ok);
	SIM_EXPECT_EQ(hostCount, 10);
	SIM_EXPECT(sim.GetDataToggle(0x81) != before);

	before = sim.GetDataToggle(0x81);
	hostCount = 0;
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, std::span<const uint8>(payload).first(64), false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::ok);
	SIM_EXPECT_EQ(hostCount, 64);
	SIM_EXPECT_EQ(log.completionCounts.Back(), 64);
	SIM_EXPECT(sim.GetDataToggle(0x81) != before);

	before = sim.GetDataToggle(0x81);
	hostCount = 0;
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, std::span<const uint8>(payload).first(64), true), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::ok);
	SIM_EXPECT_EQ(hostCount, 64);
	SIM_EXPECT(sim.GetDataToggle(0x81) == before);
}


void TestOverrunDropsWholePacket() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	Prepare(sim);

	std::array<uint8, 100> buffer = {};
	std::array<uint8, 128> data = {};
	SIM_EXPECT_EQ(sim.StartOutTransfer(0x01, buffer), ResultStatus::ok);

	// The second 64-byte packet has 36 bytes of room: it is dropped whole, after the error and before the completion.
	Result<uint32> stored = sim.HostOut(0x01, data);
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 64);
	SIM_EXPECT_EQ(log.errors.count, 1);
	SIM_EXPECT(log.errors.count == 1 && log.errors[0] == SimUSBDeviceAdapter::Error::DataOverrun);
	SIM_EXPECT(OrderIs(log, "EC"));
	SIM_EXPECT_EQ(log.completionCounts.Back(), 64);
	SIM_EXPECT(!sim.HasArmedTransfer(0x01));
}


void TestBufferFullAndZeroLengthPacket() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	Prepare(sim);

	std::array<uint8, 64> buffer = {};
	std::array<uint8, 64> data = {};
	SIM_EXPECT_EQ(sim.StartOutTransfer(0x01, buffer), ResultStatus::ok);

	// A full buffer completes on its own. The zero-length packet that follows is NAKed until the next transfer is armed.
	Result<uint32> stored = sim.HostOut(0x01, data);
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 64);
	SIM_EXPECT_EQ(log.completionCounts.Back(), 64);
	SIM_EXPECT_EQ(sim.HostOut(0x01, std::span<const uint8>()).Error(), ResultStatus::notReady);

	SIM_EXPECT_EQ(sim.StartOutTransfer(0x01, buffer), ResultStatus::ok);
	stored = sim.HostOut(0x01, std::span<const uint8>());
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 0);
	SIM_EXPECT_EQ(log.completionCounts.Back(), 0);
	SIM_EXPECT_EQ(log.completionAddresses.Back(), 0x01);
}


void TestShortOutPacketCompletes() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	Prepare(sim);

	std::array<uint8, 64> buffer = {};
	std::array<uint8, 10> data = {};
	SIM_EXPECT_EQ(sim.StartOutTransfer(0x01, buffer), ResultStatus::ok);
	Result<uint32> stored = sim.HostOut(0x01, data);
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 10);
	SIM_EXPECT_EQ(log.completionCounts.Back(), 10);
	SIM_EXPECT(!sim.HasArmedTransfer(0x01));
}


void TestStallRules() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	Prepare(sim);

	std::array<uint8, 64> payload = {};
	std::array<uint8, 256> host = {};
	uint32 hostCount = 0;
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, payload, false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Stall(0x81), ResultStatus::ok);
	SIM_EXPECT(sim.IsStalled(0x81));
	SIM_EXPECT(!sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(sim.GetStallCount(0x81), 1);
	SIM_EXPECT_EQ(log.completionCounts.count, 0);
	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::accessError);
	SIM_EXPECT_EQ(sim.HostOut(0x01, std::span<const uint8>()).Error(), ResultStatus::notReady);

	SIM_EXPECT_EQ(sim.ClearStall(0x81), ResultStatus::ok);
	SIM_EXPECT(!sim.IsStalled(0x81));
	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::notReady);
	SIM_EXPECT_EQ(sim.ClearStall(0x82), ResultStatus::notExist);
	SIM_EXPECT_EQ(sim.Stall(0x82), ResultStatus::notExist);

	SIM_EXPECT_EQ(sim.Stall(0x00), ResultStatus::ok);
	SIM_EXPECT(sim.IsStalled(0x80));
	SIM_EXPECT(sim.IsStalled(0x00));
	SIM_EXPECT_EQ(sim.GetStallCount(0x00), 1);
	SIM_EXPECT_EQ(sim.HostOut(0x00, std::span<const uint8>()).Error(), ResultStatus::accessError);
	SIM_EXPECT_EQ(sim.ClearStall(0x80), ResultStatus::ok);
	SIM_EXPECT(!sim.IsStalled(0x00));
}


void TestSetupRules() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	Prepare(sim);

	// A SETUP abandons the EP0 transfer in flight with no completion event.
	SIM_EXPECT_EQ(sim.StartInTransfer(0x80, std::span<const uint8>(), false), ResultStatus::ok);
	sim.InjectSetup(USBSetupBytes());
	SIM_EXPECT(!sim.HasArmedTransfer(0x80));
	SIM_EXPECT_EQ(log.completionCounts.count, 0);
	SIM_EXPECT_EQ(log.setups, 1);

	// A SETUP also clears an endpoint 0 halt, which both directions share.
	SIM_EXPECT_EQ(sim.Stall(0x00), ResultStatus::ok);
	SIM_EXPECT(sim.IsStalled(0x80));
	sim.InjectSetup(USBSetupBytes());
	SIM_EXPECT(!sim.IsStalled(0x80));
	SIM_EXPECT(!sim.IsStalled(0x00));
	SIM_EXPECT_EQ(log.completionCounts.count, 0);
	SIM_EXPECT_EQ(log.setups, 2);

	// A SETUP is DATA0 (USB 2.0 section 8.6.2), and both endpoint 0 toggles go back to DATA0 with it, even when no stage is
	// armed afterwards. A 64-byte IN stage with its zero-length packet leaves the IN toggle at DATA1 first.
	std::array<uint8, 64> stagePayload = {};
	std::array<uint8, 256> stageHost = {};
	uint32 stageHostCount = 0;
	SIM_EXPECT_EQ(sim.StartInTransfer(0x80, stagePayload, true), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x80, stageHost, stageHostCount), ResultStatus::ok);
	SIM_EXPECT(sim.GetDataToggle(0x80));
	sim.InjectSetup(USBSetupBytes());
	SIM_EXPECT(!sim.GetDataToggle(0x80));
	SIM_EXPECT(!sim.GetDataToggle(0x00));

	// A pending address takes effect when the status IN completes, before the core hears about the completion. The event log
	// records the address at each completion, so no event is reassigned here.
	std::array<uint8, 256> host = {};
	uint32 hostCount = 0;

	SIM_EXPECT_EQ(sim.SetAddress(200), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(sim.SetAddress(5), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.GetAddress(), 0);
	SIM_EXPECT_EQ(sim.StartInTransfer(0x80, std::span<const uint8>(), false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x80, host, hostCount), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.GetAddress(), 5);
	SIM_EXPECT_EQ(log.addressAtCompletion, 5);

	// A SETUP before the status discards the pending address.
	SIM_EXPECT_EQ(sim.SetAddress(7), ResultStatus::ok);
	sim.InjectSetup(USBSetupBytes());
	SIM_EXPECT_EQ(sim.StartInTransfer(0x80, std::span<const uint8>(), false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x80, host, hostCount), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.GetAddress(), 5);

	// A bus reset discards it too, and sets the address back to 0.
	SIM_EXPECT_EQ(sim.SetAddress(9), ResultStatus::ok);
	sim.InjectBusReset();
	SIM_EXPECT_EQ(sim.StartInTransfer(0x80, std::span<const uint8>(), false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.HostIn(0x80, host, hostCount), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.GetAddress(), 0);
}


// Closing an endpoint or all of them abandons a transfer in flight with no completion event.
void TestCloseAbandonsTransfers() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	Prepare(sim);

	std::array<uint8, 64> payload = {};
	std::array<uint8, 64> buffer = {};

	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, payload, false), ResultStatus::ok);
	std::size_t completionsBefore = log.completionCounts.count;
	std::size_t orderBefore = log.order.count;
	SIM_EXPECT_EQ(sim.CloseEndpoint(0x81), ResultStatus::ok);
	SIM_EXPECT(!sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(log.completionCounts.count, completionsBefore);
	SIM_EXPECT_EQ(log.order.count, orderBefore);

	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x81, 64)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.StartOutTransfer(0x01, buffer), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.StartInTransfer(0x81, payload, false), ResultStatus::ok);
	completionsBefore = log.completionCounts.count;
	orderBefore = log.order.count;
	sim.CloseAllEndpoints();
	SIM_EXPECT(!sim.HasArmedTransfer(0x01));
	SIM_EXPECT(!sim.HasArmedTransfer(0x81));
	SIM_EXPECT(sim.FindEndpoint(0x01) == nullptr);
	SIM_EXPECT_EQ(log.completionCounts.count, completionsBefore);
	SIM_EXPECT_EQ(log.order.count, orderBefore);
	SIM_EXPECT(!log.AnyOverflow());
}


void TestSuspendResumeAndFrames() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	USBDeviceAdapter::Parameters parameters;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	sim.InjectBusReset();

	SIM_EXPECT_EQ(sim.RemoteWakeup(), ResultStatus::notReady);
	sim.InjectSuspend();
	SIM_EXPECT_EQ(sim.RemoteWakeup(), ResultStatus::ok);

	// RemoteWakeup() returns at once. onResume comes later, when the port ends the signalling.
	SIM_EXPECT_EQ(log.resumes, 0);
	SIM_EXPECT_EQ(sim.RemoteWakeup(), ResultStatus::notReady);
	SIM_EXPECT_EQ(sim.FinishResume(), ResultStatus::ok);
	SIM_EXPECT_EQ(log.resumes, 1);
	SIM_EXPECT_EQ(sim.FinishResume(), ResultStatus::notReady);
	SIM_EXPECT_EQ(sim.RemoteWakeup(), ResultStatus::notReady);

	// Frame numbers have 11 bits, and the events stay off unless the parameters ask for them.
	sim.InjectStartOfFrame(0xFFFF);
	SIM_EXPECT_EQ(sim.GetFrameNumber(), 0x07FF);
	SIM_EXPECT_EQ(log.frames, 0);

	SimUSBDeviceAdapter framed;
	EventLog framedLog;
	Attach(framed, framedLog);
	parameters.startOfFrameEvents = true;
	SIM_EXPECT_EQ(framed.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(framed.Connect(), ResultStatus::ok);
	framed.InjectStartOfFrame(0x1805);
	SIM_EXPECT_EQ(framedLog.frames, 1);
	SIM_EXPECT_EQ(framedLog.lastFrame, 0x005);
	SIM_EXPECT_EQ(framed.GetFrameNumber(), 0x005);

	framed.InjectError(USBDeviceAdapter::Error::Crc);
	SIM_EXPECT_EQ(framedLog.errors.count, 1);
	SIM_EXPECT(framedLog.errors.count == 1 && framedLog.errors[0] == USBDeviceAdapter::Error::Crc);
}


void TestHostRequestsUnarmedEndpoint() {
	SimUSBDeviceAdapter sim;
	Prepare(sim);

	// A host IN or OUT to an endpoint that is open but not armed is NAKed. Nothing is stored and nothing is consumed.
	std::array<uint8, 256> host = {};
	uint32 hostCount = 0;
	std::array<uint8, 4> data = {};
	SIM_EXPECT_EQ(sim.HostIn(0x81, host, hostCount), ResultStatus::notReady);
	SIM_EXPECT_EQ(sim.HostOut(0x01, data).Error(), ResultStatus::notReady);
	SIM_EXPECT_EQ(hostCount, 0);
}


}


int main() {
	TestParameters();
	TestEndpointZeroAndBusReset();
	TestOpenRules();
	TestSharedNumberRules();
	TestPacketMemory();
	TestTransferRules();
	TestZeroLengthPacketRule();
	TestOverrunDropsWholePacket();
	TestBufferFullAndZeroLengthPacket();
	TestShortOutPacketCompletes();
	TestStallRules();
	TestSetupRules();
	TestCloseAbandonsTransfers();
	TestSuspendResumeAndFrames();
	TestHostRequestsUnarmedEndpoint();
	return SimTest::Finish("TestAdapterRules");
}
