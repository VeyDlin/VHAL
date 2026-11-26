// expect: raise PayloadCapacity to the value shown
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>


// A layer whose largest packet depends on an option: the check reads it from the configured type, so Extra(16) makes 48 bytes. The payload
// capacity of the Reliable below is 40.
struct WideOptions {
	std::size_t extra = 0;
};


template<WideOptions Value = WideOptions{}>
class WideLayerOf {
public:
	using Options = WideOptions;

	template<WideOptions value>
	using Configured = WideLayerOf<value>;


	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}


		constexpr Step Extra(std::size_t value) const {
			Options next = this->LastOptions();
			next.extra = value;
			return Step(this->WithLastOptions(next));
		}
	};


	static constexpr std::size_t header = 0;
	static constexpr std::size_t largestPacket = 32 + Value.extra;
	static constexpr uint32 fingerprint = 0x57494430;
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
		.Layer<WideLayerOf<>>().Extra(16)
		.Layer<VUB::ReliableOf<40, 4>>()
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
