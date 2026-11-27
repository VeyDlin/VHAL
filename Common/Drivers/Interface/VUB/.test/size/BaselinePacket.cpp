// The baseline of the packet configurations: the same calls with no VUB. The payload is sent on the link, and the packet the link
// took is handed to the handler. The link, the handler and the clock are the same code as in the configurations.
#include "SizeWire.h"


SizePacketLink sizeLink;


void SizeRun() {
	std::array<uint8, 32> payload{};
	SizeFill(payload);
	(void)sizeLink.Send(std::span<const uint8>(payload));
	const VUB::Meta meta{};
	SizeReceive(meta, std::span<const uint8>(sizeWire.data(), sizeWireLength));
}
