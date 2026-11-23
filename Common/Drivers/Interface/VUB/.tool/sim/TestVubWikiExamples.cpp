#include "SimCheck.h"
#include <Drivers/Interface/VUB/TestKit/VubContracts.h>
#include <Drivers/Interface/VUB/Vub.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// Every code snippet of the VUB wiki compiles here. The snippets are copied from .wiki/Common/Drivers/Interface/VUB/README.md, in the
// same order, each group in its own namespace because the names repeat.
namespace WikiWriting {
// A transform that appends one byte: the sum of the packet modulo 256. A packet whose sum does not match is refused.
struct SumCheck {
	static constexpr std::size_t growth = 1;
	static constexpr uint32 fingerprint = 0x53554D31;

	Result<std::size_t> Encode(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + growth) {
			return ResultStatus::invalidParameter;
		}

		uint8 sum = 0;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
			sum = static_cast<uint8>(sum + in[index]);
		}

		out[in.size()] = sum;
		return in.size() + growth;
	}

	Result<std::size_t> Decode(const VubMeta&, std::span<uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		std::size_t body = packet.size() - 1;
		uint8 sum = 0;
		for (std::size_t index = 0; index < body; index++) {
			sum = static_cast<uint8>(sum + packet[index]);
		}

		if (sum != packet[body]) {
			return ResultStatus::checksumError;
		}

		return body;
	}
};

// The packet link of the board: it sends a whole packet, or reports busy when its transmitter has no room.
struct BoardLink {
	static constexpr VubLinkTraits traits = VubLinkTraits().Framed().MaxPacket(64).FullDuplex();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};

void OnPacket(const VubMeta&, std::span<const uint8> payload) {
	(void)payload;
}

using Board = Vub<+[] {
	return VubBuilder()
		.Transform<SumCheck>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

BoardLink boardLink;
Board board(boardLink);

void BoardSend() {
	std::array<uint8, 3> hello = {1, 2, 3};
	board.Send(std::span<const uint8>(hello.data(), hello.size()));
	board.Process();
}

void CheckContract() {
	SumCheck sumCheck;
	bool passes = VubCheckTransform(sumCheck, 7, 500, 32);
	SIM_EXPECT(passes);
}
}  // namespace WikiWriting


// The chain shape of the wiki. It uses the types of the Writing a block snippets.
namespace WikiChain {
using WikiWriting::BoardLink;
using WikiWriting::OnPacket;
using WikiWriting::SumCheck;

using Board = Vub<+[] {
	return VubBuilder()
		.Transform<SumCheck>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;
}  // namespace WikiChain


// A block with options and a Step, written only against the documented contract: its options are a nested type, its Step gives the
// option method after the block, and the block is built from its options.
namespace WikiSteps {
using WikiWriting::BoardLink;
using WikiWriting::OnPacket;

// A transform that XORs each byte with a key. Its options give the key, and its Step gives the key as a method right after it.
struct XorKey {
	struct Options {
		uint8 key = 0;
	};


	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}

		constexpr Step Key(uint8 value) const {
			Options options = this->LastOptions();
			options.key = value;
			return Step(this->WithLastOptions(options));
		}
	};

	static constexpr std::size_t growth = 0;
	static constexpr uint32 fingerprint = 0x584B4559;

	uint8 key = 0;

	XorKey() = default;

	explicit XorKey(const Options& options) : key(options.key) {
	}

	Result<std::size_t> Encode(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = static_cast<uint8>(in[index] ^ key);
		}

		return in.size();
	}

	Result<std::size_t> Decode(const VubMeta&, std::span<uint8> packet) {
		for (std::size_t index = 0; index < packet.size(); index++) {
			packet[index] = static_cast<uint8>(packet[index] ^ key);
		}

		return packet.size();
	}
};

using Board = Vub<+[] {
	return VubBuilder()
		.Transform<XorKey>().Key(0x5A)
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

static_assert(VubBuilder().Transform<XorKey>().Key(0x5A).LastOptions().key == 0x5A);
static_assert(VubBuilder().Transform<XorKey>(XorKey::Options{0x5A}).LastOptions().key == 0x5A);
static_assert(VubBuilder().Transform<XorKey>().LastOptions().key == 0);
}  // namespace WikiSteps


int main() {
	WikiWriting::CheckContract();
	WikiWriting::BoardSend();
	SIM_EXPECT(WikiWriting::board.Drops().sendError == 0);
	SIM_EXPECT(WikiChain::Board::count == 2);
	SIM_EXPECT(WikiSteps::Board::count == 2);
	return SimFinish("TestVubWikiExamples");
}
