// The bootloader's use of CompactStreamingProtocol<20, 4> (Communication/BLECommunication.h): the same four callbacks, with bodies of the
// same kind. The transmit callback keeps each packet on the wire, the data callback hands the payload to the handler and ignores an empty
// fragment, the stream callback and the receive reset call the framing-lost function. The probe runs one protocol object as both ends:
// it feeds the wire back to the protocol until the wire holds no bytes that are not fed yet, so every fragment and every acknowledgement is carried and
// the message completes. The System port functions it reaches are stubs here: GetMs returns the probe tick, CriticalSection and Abort do
// nothing. The payload is 32 bytes, as in the VUB configurations, so the protocol cuts it into two fragments of 16 bytes.
#include "SizeWire.h"
#include <Drivers/Interface/User/StreamingProtocol/CompactStreamingProtocol.h>


uint64 System::GetMs() {
	return sizeTicks;
}


void System::CriticalSection(bool) {
}


void System::Abort(const char*, const char*, uint32) {
	for (;;) {
	}
}


using SizeProtocol = CompactStreamingProtocol<20, 4>;


// The count of the framing losses. The bootloader's NotifyBootloaderFramingLost is a call; the probe counts the calls, so they stay in
// the program.
static volatile uint32 sizeFramingLosses = 0;


static void SizeFramingLost() {
	sizeFramingLosses = sizeFramingLosses + 1;
}


SizeProtocol sizeProtocol(
	[](std::span<const uint8> data) -> bool {
		SizeKeep(data);
		return true;
	},
	[](uint8, std::span<const uint8> data) -> bool {
		if (data.empty()) {
			return true;
		}

		const VubMeta meta{};
		SizeReceive(meta, data);
		return true;
	},
	[](SizeProtocol::StreamResult result) -> void {
		if (result != SizeProtocol::StreamResult::Success) {
			SizeFramingLost();
		}
	},
	[]() -> void {
		SizeFramingLost();
	}
);


void SizeRun() {
	std::array<uint8, 32> payload{};
	SizeFill(payload);
	sizeWireLength = 0;
	(void)sizeProtocol.Send(payload);
	std::size_t fed = 0;
	while (fed < sizeWireLength) {
		std::size_t end = sizeWireLength;
		sizeProtocol.DataReceived(std::span<const uint8>(sizeWire.data() + fed, end - fed));
		fed = end;
		sizeProtocol.Process();
	}

	sizeProtocol.Process();
}
