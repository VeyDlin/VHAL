#include "SimCheck.h"
#include "Doubles.h"
#include <Drivers/Interface/VUB/TestKit/Contracts.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <array>
#include <cstddef>
#include <span>


// Encode of {01, 02} by hand: 01 XOR A5 = A4, 02 XOR (A5 XOR 25) = 02 XOR 80 = 82. Sum of A4 and 82 is 126 (hex), so the sum byte is
// 26; their XOR is 26. The wire bytes are A4 82 26 26.
void TestEncodeByHand() {
	XorTransform transform;
	VUB::Meta meta{};
	std::array<uint8, 2> plain = {0x01, 0x02};
	std::array<uint8, 4> wire{};
	Result<std::size_t> encoded = transform.Encode(meta, std::span<const uint8>(plain.data(), plain.size()), wire);
	SIM_EXPECT(encoded.IsOk());
	SIM_EXPECT(encoded.Value() == 4);
	const std::array<uint8, 4> expected = {0xA4, 0x82, 0x26, 0x26};
	SIM_EXPECT(wire == expected);
}


// Decode of the same wire bytes gives the plain bytes back in place, and a changed bit is an error.
void TestDecodeByHand() {
	XorTransform transform;
	VUB::Meta meta{};
	std::array<uint8, 4> wire = {0xA4, 0x82, 0x26, 0x26};
	Result<std::size_t> decoded = transform.Decode(meta, std::span<uint8>(wire.data(), wire.size()));
	SIM_EXPECT(decoded.IsOk());
	SIM_EXPECT(decoded.Value() == 2);
	SIM_EXPECT(wire[0] == 0x01);
	SIM_EXPECT(wire[1] == 0x02);

	std::array<uint8, 4> damaged = {0xA4, 0x83, 0x26, 0x26};
	SIM_EXPECT(transform.Decode(meta, std::span<uint8>(damaged.data(), damaged.size())).IsErr());
}


// The contract passes for the XOR transform on random packets, with a canary after every buffer.
void TestXorPassesContract() {
	XorTransform transform;
	SIM_EXPECT(VUB::CheckTransform(transform, 7, 500, 40));
	SIM_EXPECT(VUB::CheckTransform(transform, 99, 500, 255));
}


// A transform that checks nothing must fail the contract: a flipped bit is not an error for it.
void TestProbeFailsContract() {
	FlagProbe probe;
	SIM_EXPECT(!VUB::CheckTransform(probe, 7, 100, 40));
}


// Encode that writes one byte past the span it was given (at length + growth), and is otherwise honest.
struct OverrunEncodeXor : XorTransform {
	Result<std::size_t> Encode(const VUB::Meta& meta, std::span<const uint8> in, std::span<uint8> out) {
		Result<std::size_t> encoded = XorTransform::Encode(meta, in, out);
		if (encoded.IsOk()) {
			out.data()[in.size() + growth] = 0xEE;
		}

		return encoded;
	}
};


// Decode that writes one byte past the packet span it was given (at the packet length), and is otherwise honest.
struct OverrunDecodeXor : XorTransform {
	Result<std::size_t> Decode(const VUB::Meta& meta, std::span<uint8> packet) {
		Result<std::size_t> decoded = XorTransform::Decode(meta, packet);
		if (decoded.IsOk()) {
			packet.data()[packet.size()] = 0xEE;
		}

		return decoded;
	}
};


// A one-byte overrun of Encode or Decode on a short packet must fail the contract: the canary sits right after each span.
void TestOverrunFailsContract() {
	OverrunEncodeXor encodeOver;
	SIM_EXPECT(!VUB::CheckTransform(encodeOver, 7, 500, 40));
	OverrunDecodeXor decodeOver;
	SIM_EXPECT(!VUB::CheckTransform(decodeOver, 7, 500, 40));
}


int main() {
	TestEncodeByHand();
	TestDecodeByHand();
	TestXorPassesContract();
	TestProbeFailsContract();
	TestOverrunFailsContract();
	return SimFinish("TestVubTransform");
}
