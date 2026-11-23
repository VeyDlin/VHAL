#include "SimCheck.h"
#include "VubDoubles.h"
#include <Drivers/Interface/VUB/Vub.h>
#include <Drivers/Interface/VUB/TestKit/VubLoopback.h>
#include <Utilities/DataTypes.h>


// Blocks that miss one member of their contract. Each concept must reject them.
struct NoDecodeTransform {
	static constexpr std::size_t growth = 0;
	static constexpr uint32 fingerprint = 1;


	Result<std::size_t> Encode(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}
};


struct NoNextLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 2;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = true;


	Result<std::size_t> Add(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}


	Result<VubTake> Take(const VubMeta&, std::span<const uint8> packet) {
		return VubTake{0, packet.size() > 0};
	}


	void Process(uint32 nowUs, const VubOutbox& outbox) {
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


using StreamLink = VubStreamEnd<64, 16>;
using PacketLink = VubPacketEnd<64, 16>;


static_assert(VubTransform<XorTransform>);
static_assert(VubTransform<FlagProbe>);
static_assert(!VubTransform<NoDecodeTransform>);

static_assert(VubFraming<FlagFraming>);
static_assert(!VubFraming<XorTransform>);

static_assert(VubLayer<TestLayer>);
static_assert(!VubLayer<NoNextLayer>);

static_assert(VubStreamLink<StreamLink>);
static_assert(!VubPacketLink<StreamLink>);
static_assert(VubPacketLink<PacketLink>);
static_assert(!VubStreamLink<PacketLink>);
static_assert(VubLink<StreamLink>);
static_assert(VubLink<PacketLink>);
static_assert(!VubLink<NoTraitsLink>);
static_assert(!VubLink<int>);

static_assert(VubEngine<SyncEngine>);
static_assert(!VubEngine<AsyncEngine>);


void TestConceptsAccept() {
	SIM_EXPECT(VubTransform<XorTransform>);
	SIM_EXPECT(VubLayer<TestLayer>);
}


int main() {
	TestConceptsAccept();
	return SimFinish("TestVubConcepts");
}
