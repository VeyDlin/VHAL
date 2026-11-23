// The baseline of the byte-stream configurations: the same calls with no VUB. The payload is written to the link, and the bytes the
// link took are handed to the handler. The link, the handler and the clock are the same code as in the configurations.
#include "SizeWire.h"


SizeStreamLink sizeLink;


void SizeRun() {
	std::array<uint8, 32> payload{};
	SizeFill(payload);
	sizeWireLength = 0;
	(void)sizeLink.Write(payload);
	const VUB::Meta meta{};
	SizeReceive(meta, std::span<const uint8>(sizeWire.data(), sizeWireLength));
}
