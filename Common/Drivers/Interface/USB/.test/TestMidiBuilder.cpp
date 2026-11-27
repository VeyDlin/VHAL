#include <VHAL.h>
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <Drivers/Interface/USB/Class/MIDI/MidiFunction.h>
#include <Drivers/Interface/USB/Class/MIDI/USBMidiBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <cstddef>
#include <span>


// The USB-MIDI builder with the MidiFunction it describes. The function is built from the builder's layout, so the endpoints it opens
// are the ones the descriptors name, and the events go both ways through the simulated host. Every expected packet is written by
// hand from the USB-MIDI event packet layout (MIDI 1.0 section 4): byte 0 is cable << 4 | code index, bytes 1 to 3 the MIDI bytes.


namespace {


// Two cables on explicit endpoints: the bulk OUT 0x01 and the bulk IN 0x81.
using MidiUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("MIDI builder")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMidiBuilder()
				.Cables(2)
				.BulkOut(0x01)
				.BulkIn(0x81)));
}>;


// Two cables, 32 events in each direction.
using Midi = MidiFunction<2, 32, 32>;


struct MidiBuilderFixture {
	SimUSBDeviceAdapter sim;
	Midi midi;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	// The function is built from the layout of the builder's only function.
	MidiBuilderFixture()
		: midi(MidiUsb::Function(0)),
		  functions{ &midi },
		  device(sim, MidiUsb::Set, functions) {
	}


	// Every case ends with no critical section open.
	~MidiBuilderFixture() {
		SimAssertNoCriticalSection();
	}


	// Attaches the device and configures it, as a host does.
	void Configure() {
		SIM_EXPECT_EQ(device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
		sim.InjectBusReset();
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(device.GetState(), USBDeviceState::Configured);
	}


	// Sends the host's OUT packets as one data stage on the built OUT endpoint, then runs the device loop so the function takes them.
	ResultStatus HostSend(std::span<const uint8> data) {
		Result<uint32> stored = sim.HostOut(0x01, data);
		if (!stored.IsOk()) {
			return stored.Error();
		}

		device.Process();
		return ResultStatus::ok;
	}


	// Reads the built IN endpoint until the host gets no more. Process() runs before each read, as the application loop does.
	std::size_t ReadIn(std::span<uint8> out) {
		std::size_t total = 0;
		for (int step = 0; step < 64; step++) {
			device.Process();
			uint32 received = 0;
			if (sim.HostIn(0x81, out.subspan(total), received) != ResultStatus::ok) {
				break;
			}

			total += received;
		}

		return total;
	}
};


}


// The host sends a NoteOn on cable 1 and a control change on cable 0 on the built OUT endpoint. Both come out of the receive queue in
// order, with their cable numbers.
void TestHostToDevice() {
	MidiBuilderFixture fixture;
	fixture.Configure();

	constexpr uint8 received[] = {
		0x19, 0x90, 0x3C, 0x7F,
		0x0B, 0xB0, 0x07, 0x64
	};

	SIM_EXPECT_EQ(fixture.HostSend(received), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 2);

	USBMidiPacket packet;
	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::ok);
	SIM_EXPECT_EQ(packet.Cable(), 1);
	SIM_EXPECT_EQ(packet.CodeIndex(), 0x9);
	SIM_EXPECT_EQ(packet.bytes[1], 0x90);
	SIM_EXPECT_EQ(packet.bytes[2], 0x3C);
	SIM_EXPECT_EQ(packet.bytes[3], 0x7F);

	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::ok);
	SIM_EXPECT_EQ(packet.Cable(), 0);
	SIM_EXPECT_EQ(packet.CodeIndex(), 0xB);
	SIM_EXPECT_EQ(packet.bytes[1], 0xB0);
	SIM_EXPECT_EQ(packet.bytes[2], 0x07);
	SIM_EXPECT_EQ(packet.bytes[3], 0x64);
}


// The device sends a NoteOn on cable 1 and a control change on cable 0. The host reads them from the built IN endpoint, in order.
void TestDeviceToHost() {
	MidiBuilderFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(fixture.midi.NoteOn(1, 0, 60, 100), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.ControlChange(0, 0, 7, 100), ResultStatus::ok);

	constexpr uint8 expected[] = {
		0x19, 0x90, 0x3C, 0x64,
		0x0B, 0xB0, 0x07, 0x64
	};

	std::array<uint8, 16> out{};
	std::size_t total = fixture.ReadIn(out);
	SIM_EXPECT_EQ(total, sizeof(expected));
	SIM_EXPECT(total == sizeof(expected) && SimHost::SameBytes(std::span<const uint8>(out).first(total), std::span<const uint8>(expected)));
}


// A SysEx message of four bytes on cable 1 takes two packets: a start packet with three bytes, then an end packet with one byte.
void TestSysExToHost() {
	MidiBuilderFixture fixture;
	fixture.Configure();

	constexpr uint8 message[] = {
		0xF0, 0x7D, 0x01, 0xF7
	};

	SIM_EXPECT_EQ(fixture.midi.SendSysEx(1, message), ResultStatus::ok);

	constexpr uint8 expected[] = {
		0x14, 0xF0, 0x7D, 0x01,
		0x15, 0xF7, 0x00, 0x00
	};

	std::array<uint8, 16> out{};
	std::size_t total = fixture.ReadIn(out);
	SIM_EXPECT_EQ(total, sizeof(expected));
	SIM_EXPECT(total == sizeof(expected) && SimHost::SameBytes(std::span<const uint8>(out).first(total), std::span<const uint8>(expected)));
}


int main() {
	TestHostToDevice();
	TestDeviceToHost();
	TestSysExToHost();
	return SimTest::Finish("TestMidiBuilder");
}
