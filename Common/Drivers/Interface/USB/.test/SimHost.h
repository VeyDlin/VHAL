#pragma once
#include <VHAL.h>
#include <Adapter/Utilities/USBTypes.h>
#include "SimUSBDeviceAdapter.h"
#include <span>


// The host side of the tests: SETUP packets as the bus carries them, and whole control requests run the way a host runs
// them (SETUP, then each armed stage in turn, until none is armed).
namespace SimHost {


// Lays out the eight SETUP bytes by hand: bmRequestType, bRequest, then wValue, wIndex and wLength, little-endian.
inline USBSetupBytes MakeSetup(uint8 requestType, uint8 request, uint16 value, uint16 index, uint16 length) {
	USBSetupBytes setup;
	setup.bytes[0] = requestType;
	setup.bytes[1] = request;
	setup.bytes[2] = static_cast<uint8>(value & 0xFF);
	setup.bytes[3] = static_cast<uint8>(value >> 8);
	setup.bytes[4] = static_cast<uint8>(index & 0xFF);
	setup.bytes[5] = static_cast<uint8>(index >> 8);
	setup.bytes[6] = static_cast<uint8>(length & 0xFF);
	setup.bytes[7] = static_cast<uint8>(length >> 8);
	return setup;
}


// Runs one control request. Returns ok only when the request ended with its status stage: a zero-length completion on 0x00
// after an IN request with data, or on 0x80 otherwise. A request the core stalled returns accessError. A request the core
// left unfinished returns notReady: a stage still armed after the bound, no endpoint 0 completion at all, a status stage
// missing, or hostData that no OUT stage took. So a core that drops a request or half-completes it fails the test that calls
// this, instead of passing it on the strength of a later state check.
//
// hostData is sent in an OUT data stage. Bytes received in an IN data stage are written to in from its start, and received is
// set to their count. A NAK or STALL of a host transaction is returned as it is.
inline ResultStatus Control(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup, std::span<const uint8> hostData, std::span<uint8> in, uint32& received) {
	// Three stages (data, status, and a late OUT data stage) are the most a request in these tests has. A core that keeps arming
	// stages is still armed after this bound, and that is reported as notReady below.
	constexpr uint32 MaxStages = 8;

	received = 0;
	uint32 completionsBefore = sim.GetControlCompletionCount();
	sim.InjectSetup(setup);

	if (sim.IsStalled(USBEndpoint::ControlIn)) {
		return ResultStatus::accessError;
	}

	bool hostDataPending = !hostData.empty();

	for (uint32 step = 0; step < MaxStages; step++) {
		if (sim.HasArmedTransfer(USBEndpoint::ControlIn)) {
			uint32 count = 0;
			ResultStatus status = sim.HostIn(USBEndpoint::ControlIn, in.subspan(received), count);

			if (status != ResultStatus::ok) {
				return status;
			}

			received += count;
		} else if (sim.HasArmedTransfer(USBEndpoint::ControlOut)) {
			std::span<const uint8> sent = hostDataPending ? hostData : std::span<const uint8>();
			hostDataPending = false;
			Result<uint32> stored = sim.HostOut(USBEndpoint::ControlOut, sent);

			if (!stored.IsOk()) {
				return stored.Error();
			}
		} else {
			break;
		}
	}

	if (sim.IsStalled(USBEndpoint::ControlIn)) {
		return ResultStatus::accessError;
	}

	if (sim.HasArmedTransfer(USBEndpoint::ControlIn) || sim.HasArmedTransfer(USBEndpoint::ControlOut) || hostDataPending) {
		return ResultStatus::notReady;
	}

	if (sim.GetControlCompletionCount() == completionsBefore) {
		return ResultStatus::notReady;
	}

	// The status stage of an IN request with data is an OUT (0x00). Every other request ends with an IN status (0x80).
	uint16 length = static_cast<uint16>(setup.bytes[6] | (setup.bytes[7] << 8));
	bool inWithData = (setup.bytes[0] & 0x80) != 0 && length > 0;
	uint8 statusAddress = inWithData ? USBEndpoint::ControlOut : USBEndpoint::ControlIn;
	SimUSBDeviceAdapter::ControlCompletion last = sim.GetLastControlCompletion();

	if (last.address != statusAddress || last.byteCount != 0) {
		return ResultStatus::notReady;
	}

	return ResultStatus::ok;
}


// Same as Control() for a request with no data stage in either direction.
inline ResultStatus Control(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup) {
	uint32 received = 0;
	return Control(sim, setup, std::span<const uint8>(), std::span<uint8>(), received);
}


inline bool SameBytes(std::span<const uint8> actual, std::span<const uint8> expected) {
	if (actual.size() != expected.size()) {
		return false;
	}

	for (std::size_t index = 0; index < expected.size(); index++) {
		if (actual[index] != expected[index]) {
			return false;
		}
	}

	return true;
}


}
