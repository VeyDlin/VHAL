#pragma once
#include <VHAL.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <cstddef>
#include <cstdio>
#include <span>


// The helpers the device tests share: attaching a device the way a host does, and comparing two byte sequences. The expected bytes are
// not here; each test writes its own from the specification.
namespace SimTest {


// Resets the bus, sets the address and selects configuration 1, as a host does after a reset. Each control request must succeed and the
// device must be configured afterwards, so a failed enumeration fails the test that asked for it.
inline void EnumerateDevice(SimUSBDeviceAdapter& sim, USBDevice& device) {
	sim.InjectBusReset();
	SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(device.GetState(), USBDeviceState::Configured);
}


// Compares two byte sequences and prints the offset of the first difference. The check counts as one failure when they differ.
inline void ExpectBytes(std::span<const uint8> actual, std::span<const uint8> expected, const char* expression, const char* file, int line) {
	bool same = SimHost::SameBytes(actual, expected);
	if (!same) {
		std::size_t offset = 0;
		while (offset < actual.size() && offset < expected.size() && actual[offset] == expected[offset]) {
			offset++;
		}

		std::fprintf(stderr, "%s:%d: %s differs: %zu bytes against %zu, first difference at %zu\n",
			file, line, expression, actual.size(), expected.size(), offset);
	}

	SimTest::Expect(same, expression, file, line);
}


}


#define SIM_EXPECT_BYTES(actual, expected) SimTest::ExpectBytes((actual), (expected), #actual, __FILE__, __LINE__)
