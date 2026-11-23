// The bootloader's use of CompactStreamingProtocol<20, 4> (Communication/BLECommunication.h): the protocol sends each fragment through a
// transmit callback to a packet link, takes the received data through DataReceived, and calls Process. The System port functions it
// reaches are stubs here: GetMs returns the probe tick, CriticalSection and Abort do nothing. The payload is 32 bytes, as in the VUB
// configurations, so the protocol cuts it into fragments.
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


SizePacketLink sizeLink;


SizeProtocol sizeProtocol(
	[](std::span<const uint8> data) -> bool {
		return sizeLink.Send(data) == ResultStatus::ok;
	},
	[](uint8, std::span<const uint8> data) -> bool {
		const VubMeta meta{};
		SizeReceive(meta, data);
		return true;
	},
	[](SizeProtocol::StreamResult) -> void {
	}
);


void SizeRun() {
	std::array<uint8, 32> payload{};
	SizeFill(payload);
	sizeWireLength = 0;
	(void)sizeProtocol.Send(payload);
	sizeProtocol.Process();
	sizeProtocol.DataReceived(std::span<const uint8>(sizeWire.data(), sizeWireLength));
	sizeProtocol.Process();
}
