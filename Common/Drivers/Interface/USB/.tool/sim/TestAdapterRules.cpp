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
	SimUSBDeviceAdapter* sim = nullptr;


	bool AnyOverflow() const {
		return completionAddresses.overflowed || completionCounts.overflowed || errors.overflowed || order.overflowed;
	}


	void OnBusReset(USBSpeed) {
		busResets++;
	}


	void OnSetup(const USBSetupBytes&) {
		setups++;
	}


	void OnTransferComplete(uint8 address, uint32 byteCount) {
		completionAddresses.Push(address);
		completionCounts.Push(byteCount);
		order.Push('C');
		addressAtCompletion = sim->GetAddress();
	}


	void OnError(SimUSBDeviceAdapter::Error error) {
		errors.Push(error);
		order.Push('E');
	}


	void OnResume() {
		resumes++;
	}


	void OnStartOfFrame(uint16 frameNumber) {
		frames++;
		lastFrame = frameNumber;
	}
};


bool OrderIs(const EventLog& log, std::string_view expected) {
	return std::string_view(log.order.items.data(), log.order.count) == expected;
}


void Attach(SimUSBDeviceAdapter& sim, EventLog& log) {
	log.sim = &sim;
	sim.onBusReset = Callback::Delegate<void(USBSpeed)>::Bind<EventLog, &EventLog::OnBusReset>(&log);
	sim.onSetup = Callback::Delegate<void(const USBSetupBytes&)>::Bind<EventLog, &EventLog::OnSetup>(&log);
	sim.onTransferComplete = Callback::Delegate<void(uint8, uint32)>::Bind<EventLog, &EventLog::OnTransferComplete>(&log);
	sim.onError = Callback::Delegate<void(SimUSBDeviceAdapter::Error)>::Bind<EventLog, &EventLog::OnError>(&log);
	sim.onResume = Callback::Delegate<void()>::Bind<EventLog, &EventLog::OnResume>(&log);
	sim.onStartOfFrame = Callback::Delegate<void(uint16)>::Bind<EventLog, &EventLog::OnStartOfFrame>(&log);
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

	// Isochronous: up to 1023 bytes, and no doubleBuffer flag, because the port always gives an isochronous endpoint its two buffers.
	USBDeviceAdapter::EndpointConfig isochronous = Bulk(0x83, 64);
	isochronous.type = USBEndpointType::Isochronous;
	isochronous.maxPacketSize = 1024;
	SIM_EXPECT_EQ(sim.OpenEndpoint(isochronous), ResultStatus::invalidParameter);
	isochronous.maxPacketSize = 64;
	isochronous.doubleBuffer = true;
	SIM_EXPECT_EQ(sim.OpenEndpoint(isochronous), ResultStatus::invalidParameter);
	isochronous.doubleBuffer = false;
	SIM_EXPECT_EQ(sim.OpenEndpoint(isochronous), ResultStatus::ok);
	SIM_EXPECT(sim.IsDoubleBuffered(0x83));

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

	// The buffer table (64 bytes) and endpoint 0 (two 64-byte buffers) take 192 of the 1024, which leaves 832 for the endpoints. Six
	// double-buffered 64-byte IN endpoints take 768 of them. The seventh needs 128 more and only 64 are left, so it is outOfMemory. The port
	// refuses it the same way: its second buffer finds no 64-byte block above the sixth endpoint's (FindPmaBlock()).
	for (uint8 number = 1; number <= 6; number++) {
		USBDeviceAdapter::EndpointConfig config = Bulk(USBEndpoint::MakeIn(number), 64);
		config.doubleBuffer = true;
		SIM_EXPECT_EQ(sim.OpenEndpoint(config), ResultStatus::ok);
	}

	USBDeviceAdapter::EndpointConfig seventh = Bulk(0x87, 64);
	seventh.doubleBuffer = true;
	SIM_EXPECT_EQ(sim.OpenEndpoint(seventh), ResultStatus::outOfMemory);
	SIM_EXPECT(sim.FindEndpoint(0x87) == nullptr);

	// The OUT on number 1 shares its register with the double-buffered IN, so the shared-number rule refuses it before packet
	// memory is looked at.
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x01, 64)), ResultStatus::invalidParameter);

	// Closing number 1 gives back 128 bytes, and the single-buffered OUT fits beside the other five.
	SIM_EXPECT_EQ(sim.CloseEndpoint(0x81), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x01, 64)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.CloseEndpoint(0x81), ResultStatus::notExist);
	SIM_EXPECT_EQ(sim.CloseEndpoint(0x00), ResultStatus::invalidParameter);
}


// The isochronous packet memory beside the buffer table and endpoint 0. An isochronous endpoint takes two blocks of its packet size, so 416 bytes
// take 2 * 416 = 832 for an IN endpoint, which is the whole budget: the port places them at 192 and 608. An OUT block is rounded to 32 bytes above
// 62, so a 417-byte OUT packet needs two blocks of 448, and the port refuses it with outOfMemory (TestIsoLargestPacketBesideEndpointZero() in
// TestFsdevPort.cpp). The adapter contract is the same on both.
void TestIsochronousPacketMemoryLimit() {
	SimUSBDeviceAdapter sim;
	USBDeviceAdapter::Parameters parameters;
	parameters.endpointCount = SimUSBDeviceAdapter::HardwareEndpointCount;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	sim.InjectBusReset();

	USBDeviceAdapter::EndpointConfig largest;
	largest.address = 0x81;
	largest.type = USBEndpointType::Isochronous;
	largest.maxPacketSize = 416;
	SIM_EXPECT_EQ(sim.OpenEndpoint(largest), ResultStatus::ok);
	SIM_EXPECT(sim.FindEndpoint(0x81) != nullptr);

	USBDeviceAdapter::EndpointConfig tooLarge = largest;
	tooLarge.address = 0x82;
	tooLarge.maxPacketSize = 417;
	SIM_EXPECT_EQ(sim.OpenEndpoint(tooLarge), ResultStatus::outOfMemory);
	SIM_EXPECT(sim.FindEndpoint(0x82) == nullptr);
	SIM_EXPECT(sim.FindEndpoint(0x81) != nullptr);
}


// The port places every block at a 4-byte boundary, so a block of 62 bytes takes 64 with its gap. Six isochronous IN endpoints of 62 bytes take two
// blocks each: the first at 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832 and 896, and the last one ends at 958. A 44-byte endpoint then
// needs two blocks of 44: the first fits at 960, but the second would end at 1048, past the 1024 bytes. The port refuses it with outOfMemory, and
// so does the simulator. The sum of the sizes is 6 * 124 + 88 = 832, so a budget that only adds sizes would wrongly accept it. The same endpoint
// fits when only five of the 62-byte ones are open, so the refusal is the placement and not the size.
void TestIsochronousBlockAlignmentLimit() {
	SimUSBDeviceAdapter sim;
	USBDeviceAdapter::Parameters parameters;
	parameters.endpointCount = SimUSBDeviceAdapter::HardwareEndpointCount;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	sim.InjectBusReset();

	for (uint8 number = 1; number <= 6; number++) {
		USBDeviceAdapter::EndpointConfig config;
		config.address = USBEndpoint::MakeIn(number);
		config.type = USBEndpointType::Isochronous;
		config.maxPacketSize = 62;
		SIM_EXPECT_EQ(sim.OpenEndpoint(config), ResultStatus::ok);
	}

	USBDeviceAdapter::EndpointConfig fortyFour;
	fortyFour.address = 0x87;
	fortyFour.type = USBEndpointType::Isochronous;
	fortyFour.maxPacketSize = 44;
	SIM_EXPECT_EQ(sim.OpenEndpoint(fortyFour), ResultStatus::outOfMemory);
	SIM_EXPECT(sim.FindEndpoint(0x87) == nullptr);

	// Closing one of the 62-byte endpoints frees its two blocks, and the 44-byte endpoint then fits in the first gap that holds both.
	SIM_EXPECT_EQ(sim.CloseEndpoint(0x86), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(fortyFour), ResultStatus::ok);
	SIM_EXPECT(sim.FindEndpoint(0x87) != nullptr);
}


USBDeviceAdapter::EndpointConfig IsoEndpoint(uint8 address, uint16 maxPacketSize) {
	USBDeviceAdapter::EndpointConfig config;
	config.address = address;
	config.type = USBEndpointType::Isochronous;
	config.maxPacketSize = maxPacketSize;
	return config;
}


// A connected, reset simulator. With useHighest the highest endpoint number is set first, as the device core does before SetParameters().
void StartSim(SimUSBDeviceAdapter& sim, bool useHighest, uint8 highest) {
	if (useHighest) {
		SIM_EXPECT_EQ(sim.SetHighestEndpointNumber(highest), ResultStatus::ok);
	}

	USBDeviceAdapter::Parameters parameters;
	parameters.endpointCount = SimUSBDeviceAdapter::HardwareEndpointCount;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	sim.InjectBusReset();
}


// The buffer table covers the numbers 0 to the highest one set. With the numbers 0 to 2 (24 bytes), endpoint 0 ends at 152, and two 436-byte IN
// blocks fit, at 152 and 588. With the full table (64 bytes) endpoint 0 ends at 192, and the second block would end at 1064.
void TestHighestEndpointSizesTable() {
	SimUSBDeviceAdapter small;
	StartSim(small, true, 2);
	SIM_EXPECT_EQ(small.OpenEndpoint(IsoEndpoint(0x81, 436)), ResultStatus::ok);

	SimUSBDeviceAdapter full;
	StartSim(full, false, 0);
	SIM_EXPECT_EQ(full.OpenEndpoint(IsoEndpoint(0x81, 436)), ResultStatus::outOfMemory);
	SIM_EXPECT(full.FindEndpoint(0x81) == nullptr);
}


// The call is refused for a number the hardware does not have, while connected, and while an endpoint is open even when detached. An endpoint
// above the highest number is invalidParameter.
void TestHighestEndpointCallRules() {
	SimUSBDeviceAdapter tooHigh;
	SIM_EXPECT_EQ(tooHigh.SetHighestEndpointNumber(8), ResultStatus::invalidParameter);

	SimUSBDeviceAdapter started;
	StartSim(started, true, 2);
	SIM_EXPECT_EQ(started.SetHighestEndpointNumber(7), ResultStatus::busy);
	SIM_EXPECT_EQ(started.OpenEndpoint(Bulk(0x83, 64)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(started.OpenEndpoint(Bulk(0x82, 64)), ResultStatus::ok);

	SimUSBDeviceAdapter detached;
	StartSim(detached, false, 0);
	SIM_EXPECT_EQ(detached.OpenEndpoint(Bulk(0x85, 64)), ResultStatus::ok);
	SIM_EXPECT_EQ(detached.Disconnect(), ResultStatus::ok);
	SIM_EXPECT_EQ(detached.SetHighestEndpointNumber(2), ResultStatus::busy);
}


// The table is sized when SetParameters() places endpoint 0, so a call after that waits for the next SetParameters(). Here the call asks for 7
// once endpoint 0 sits above a table of the numbers 0 to 2: endpoint 3 is still refused. The next SetParameters() applies 7, and endpoint 3 opens.
void TestHighestEndpointAppliesAtSetParameters() {
	SimUSBDeviceAdapter sim;
	SIM_EXPECT_EQ(sim.SetHighestEndpointNumber(2), ResultStatus::ok);
	USBDeviceAdapter::Parameters parameters;
	parameters.endpointCount = SimUSBDeviceAdapter::HardwareEndpointCount;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.SetHighestEndpointNumber(7), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x83, 64)), ResultStatus::invalidParameter);

	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(Bulk(0x83, 64)), ResultStatus::ok);
}


// The IN blocks are their byte lengths: two 196-byte IN endpoints take 2 * 196 each, at 192 to 584 and 584 to 976. A third one does not fit.
void TestIsochronousInBlocksAreExact() {
	SimUSBDeviceAdapter sim;
	StartSim(sim, false, 0);
	SIM_EXPECT_EQ(sim.OpenEndpoint(IsoEndpoint(0x81, 196)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(IsoEndpoint(0x83, 196)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(IsoEndpoint(0x85, 196)), ResultStatus::outOfMemory);
	SIM_EXPECT(sim.FindEndpoint(0x85) == nullptr);
}


// An OUT block is 224 bytes for a 196-byte packet, so an OUT endpoint of 196 bytes takes 2 * 224 = 448. Two of them, 896 bytes, do not fit beside
// the full table and endpoint 0.
// A 100-byte OUT endpoint takes 128-byte blocks, and it does fit after the first.
void TestIsochronousOutBlocksAreRounded() {
	SimUSBDeviceAdapter sim;
	StartSim(sim, false, 0);
	SIM_EXPECT_EQ(sim.OpenEndpoint(IsoEndpoint(0x02, 196)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(IsoEndpoint(0x04, 196)), ResultStatus::outOfMemory);
	SIM_EXPECT_EQ(sim.OpenEndpoint(IsoEndpoint(0x04, 100)), ResultStatus::ok);
}


// The stereo pair, a 196-byte microphone and a 196-byte speaker, needs 24 + 128 + 392 + 448 = 992 bytes. The full table makes it 1032, so the
// speaker is refused there, and the table of the numbers 0 to 2 makes the pair fit.
void TestStereoPairFitsWithSmallTable() {
	SimUSBDeviceAdapter small;
	StartSim(small, true, 2);
	SIM_EXPECT_EQ(small.OpenEndpoint(IsoEndpoint(0x81, 196)), ResultStatus::ok);
	SIM_EXPECT_EQ(small.OpenEndpoint(IsoEndpoint(0x02, 196)), ResultStatus::ok);

	SimUSBDeviceAdapter full;
	StartSim(full, false, 0);
	SIM_EXPECT_EQ(full.OpenEndpoint(IsoEndpoint(0x81, 196)), ResultStatus::ok);
	SIM_EXPECT_EQ(full.OpenEndpoint(IsoEndpoint(0x02, 196)), ResultStatus::outOfMemory);
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


// An isochronous endpoint has no halt. Stall() is invalidParameter and changes nothing, and ClearStall() is ok and changes nothing. A direction
// that is not open is notExist for both, as for the other types.
void TestIsochronousHaltRules() {
	SimUSBDeviceAdapter sim;
	EventLog log;
	Attach(sim, log);
	USBDeviceAdapter::Parameters parameters;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	sim.InjectBusReset();

	USBDeviceAdapter::EndpointConfig isochronous = Bulk(0x83, 64);
	isochronous.type = USBEndpointType::Isochronous;
	SIM_EXPECT_EQ(sim.OpenEndpoint(isochronous), ResultStatus::ok);

	SIM_EXPECT_EQ(sim.Stall(0x83), ResultStatus::invalidParameter);
	SIM_EXPECT(!sim.IsStalled(0x83));
	SIM_EXPECT_EQ(sim.GetStallCount(0x83), 0);
	SIM_EXPECT_EQ(sim.ClearStall(0x83), ResultStatus::ok);
	SIM_EXPECT(!sim.IsStalled(0x83));
	SIM_EXPECT_EQ(sim.GetStallCount(0x83), 0);
	SIM_EXPECT_EQ(sim.Stall(0x03), ResultStatus::notExist);
	SIM_EXPECT_EQ(sim.ClearStall(0x03), ResultStatus::notExist);
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
	TestIsochronousPacketMemoryLimit();
	TestIsochronousBlockAlignmentLimit();
	TestHighestEndpointSizesTable();
	TestHighestEndpointCallRules();
	TestHighestEndpointAppliesAtSetParameters();
	TestIsochronousInBlocksAreExact();
	TestIsochronousOutBlocksAreRounded();
	TestStereoPairFitsWithSmallTable();
	TestTransferRules();
	TestZeroLengthPacketRule();
	TestOverrunDropsWholePacket();
	TestBufferFullAndZeroLengthPacket();
	TestShortOutPacketCompletes();
	TestStallRules();
	TestIsochronousHaltRules();
	TestSetupRules();
	TestCloseAbandonsTransfers();
	TestSuspendResumeAndFrames();
	TestHostRequestsUnarmedEndpoint();
	return SimTest::Finish("TestAdapterRules");
}
