#include "SimAssert.h"
#include "../common/BenchProtocol.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// BenchProtocol::PatternGenerator::Fill() writes whole words inside the device's USB interrupt, so it must produce exactly the
// bytes NextByte() produces, from any position in the word stream and for any length. The cases cover every position inside a
// word (offsets 0 to 19 cover five words), every length from 0 to 2048 (the transfer size), and several start states, because
// the fill must not depend on the seed. The word store has two forms, and both are checked against NextByte() as well.


namespace {


using Generator = BenchProtocol::PatternGenerator;


// Guard bytes around the filled span: a fill that writes one byte too far or too early is caught here.
constexpr std::size_t GuardSize = 4;
constexpr std::uint8_t GuardValue = 0xA5;


// Advances a generator through NextByte(), so the start state is an ordinary position in the stream with bytesLeft set
// by the offset.
Generator MakeGenerator(std::uint32_t seed, std::size_t offset) {
	Generator generator;
	generator.state = seed;
	for (std::size_t index = 0; index < offset; index++) {
		generator.NextByte();
	}

	return generator;
}


bool SameGeneratorState(const Generator& left, const Generator& right) {
	return left.state == right.state && left.word == right.word && left.bytesLeft == right.bytesLeft;
}


// Fills a generator with Fill() and an identical copy with NextByte(), then compares the bytes, the guards and the state.
// Returns false on any difference, so the caller can count the failing cases.
bool CheckFill(std::uint32_t seed, std::size_t offset, std::size_t length) {
	Generator fast = MakeGenerator(seed, offset);
	Generator reference = fast;

	std::vector<std::uint8_t> buffer(length + 2 * GuardSize, GuardValue);
	std::span<std::uint8_t> window(buffer.data() + GuardSize, length);
	fast.Fill(window);

	std::vector<std::uint8_t> expected(length);
	for (std::size_t index = 0; index < length; index++) {
		expected[index] = reference.NextByte();
	}

	for (std::size_t index = 0; index < length; index++) {
		if (buffer[GuardSize + index] != expected[index]) {
			return false;
		}
	}

	for (std::size_t index = 0; index < GuardSize; index++) {
		if (buffer[index] != GuardValue || buffer[GuardSize + length + index] != GuardValue) {
			return false;
		}
	}

	return SameGeneratorState(fast, reference);
}


// Counts the (offset, length) cases that differ for one start state.
std::size_t CountFailures(std::uint32_t seed, std::size_t maxOffset, std::size_t maxLength) {
	std::size_t failures = 0;
	for (std::size_t offset = 0; offset < maxOffset; offset++) {
		for (std::size_t length = 0; length <= maxLength; length++) {
			if (!CheckFill(seed, offset, length)) {
				failures++;
			}
		}
	}

	return failures;
}


// Checks both forms of WriteWordBytes() for one word value. The expected bytes are NextByte() output for a generator whose
// current word is the value, so the store is checked against the byte generator itself. The littleEndian form is run here
// on the little-endian PC; on the firmware it is used only when the target is little-endian.
bool CheckWordBytes(std::uint32_t word) {
	Generator reference;
	reference.word = word;
	reference.bytesLeft = 4;

	std::array<std::uint8_t, 4> expected{};
	for (std::size_t index = 0; index < expected.size(); index++) {
		expected[index] = reference.NextByte();
	}

	std::array<std::uint8_t, 4> memoryForm{};
	std::array<std::uint8_t, 4> shiftForm{};
	Generator::WriteWordBytes<true>(memoryForm.data(), word);
	Generator::WriteWordBytes<false>(shiftForm.data(), word);
	return memoryForm == expected && shiftForm == expected;
}


}


int main() {
	// Start states: the protocol seed, the smallest state, the largest state, and two arbitrary ones. Zero is not a valid state.
	constexpr std::array<std::uint32_t, 5> Seeds{
		BenchProtocol::PatternSeed,
		0x00000001,
		0xFFFFFFFF,
		0x80000000,
		0xDEADBEEF
	};

	// Every offset inside five words, and every length up to the transfer size, from each start state.
	for (std::uint32_t seed : Seeds) {
		SIM_EXPECT_EQ(CountFailures(seed, 20, 2048), 0);
	}

	// Transfer-sized fills at every offset inside a word, from the seed and from a state in the middle of the stream.
	constexpr std::array<std::size_t, 5> TransferLengths{ 2048, 2047, 2049, 2044, 2052 };
	for (std::size_t length : TransferLengths) {
		for (std::size_t offset = 0; offset < 4; offset++) {
			SIM_EXPECT(CheckFill(BenchProtocol::PatternSeed, offset, length));
			SIM_EXPECT(CheckFill(0xDEADBEEF, offset, length));
		}
	}

	// A fill that continues where the previous one stopped must give the same stream as one long NextByte() run, so the
	// pieces the bench writes in separate calls join without a gap.
	Generator split = MakeGenerator(BenchProtocol::PatternSeed, 0);
	Generator whole = split;
	std::vector<std::uint8_t> splitBytes(300);
	std::size_t position = 0;
	constexpr std::array<std::size_t, 6> Pieces{ 1, 3, 4, 0, 65, 227 };
	for (std::size_t piece : Pieces) {
		split.Fill(std::span<std::uint8_t>(splitBytes.data() + position, piece));
		position += piece;
	}

	bool joined = true;
	for (std::size_t index = 0; index < position; index++) {
		if (splitBytes[index] != whole.NextByte()) {
			joined = false;
		}
	}

	SIM_EXPECT(joined);
	SIM_EXPECT(SameGeneratorState(split, whole));

	// Both word-store forms against NextByte(), for edge values and for a run of words from the protocol seed.
	constexpr std::array<std::uint32_t, 5> EdgeWords{ 0x00000000, 0x00000001, 0x01020304, 0x80000000, 0xFFFFFFFF };
	for (std::uint32_t word : EdgeWords) {
		SIM_EXPECT(CheckWordBytes(word));
	}

	std::uint32_t wordState = BenchProtocol::PatternSeed;
	for (std::size_t index = 0; index < 1000; index++) {
		SIM_EXPECT(CheckWordBytes(BenchProtocol::NextWord(wordState)));
	}

	return SimTest::Finish("TestBenchFill");
}
