#include "SimCheck.h"
#include "Doubles.h"
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>


// Blocks that miss one member of their contract. Each concept must reject them.
struct NoDecodeTransform {
	static constexpr std::size_t growth = 0;
	static constexpr uint32 fingerprint = 1;


	Result<std::size_t> Encode(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}
};


struct NoNextLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 2;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = true;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		return VUB::Taken{0, packet.size() > 0};
	}


	void Process(uint32 nowUs, const VUB::Outbox& outbox) {
		(void)nowUs;
		(void)outbox;
	}
};


struct SyncEngine {
	static constexpr bool synchronous = true;
};


struct AsyncEngine {
	static constexpr bool synchronous = false;
};


// A link with a stream form and no traits: it is not a link at all.
struct NoTraitsLink {
	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


using StreamLink = VUB::StreamEnd<64, 16>;
using PacketLink = VUB::PacketEnd<64, 16>;


static_assert(VUB::TransformBlock<XorTransform>);
static_assert(VUB::TransformBlock<FlagProbe>);
static_assert(!VUB::TransformBlock<NoDecodeTransform>);

static_assert(VUB::FramingBlock<FlagFraming>);
static_assert(!VUB::FramingBlock<XorTransform>);

static_assert(VUB::LayerBlock<TestLayer>);
static_assert(!VUB::LayerBlock<NoNextLayer>);

static_assert(VUB::StreamLink<StreamLink>);
static_assert(!VUB::PacketLink<StreamLink>);
static_assert(VUB::PacketLink<PacketLink>);
static_assert(!VUB::StreamLink<PacketLink>);
static_assert(VUB::LinkBlock<StreamLink>);
static_assert(VUB::LinkBlock<PacketLink>);
static_assert(!VUB::LinkBlock<NoTraitsLink>);
static_assert(!VUB::LinkBlock<int>);

static_assert(VUB::EngineBlock<SyncEngine>);
static_assert(!VUB::EngineBlock<AsyncEngine>);


void TestConceptsAccept() {
	SIM_EXPECT(VUB::TransformBlock<XorTransform>);
	SIM_EXPECT(VUB::LayerBlock<TestLayer>);
}


int main() {
	TestConceptsAccept();
	return SimFinish("TestVubConcepts");
}
