// expect: raise PayloadCapacity to the value shown
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>


// A layer whose packets are 48 bytes, over Reliable with a 40-byte payload capacity: the message names 48, the value to set.
struct WideLayer {
	static constexpr std::size_t header = 0;
	static constexpr std::size_t largestPacket = 48;
	static constexpr uint32 fingerprint = 0x57494445;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8>) {
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<WideLayer>()
		.Layer<VUB::ReliableOf<40, 4>>()
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
