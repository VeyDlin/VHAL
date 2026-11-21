#include <VHAL.h>
#include <Drivers/Interface/USB/Class/CDC/NcmNtb16.h>
#include "SimAssert.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <vector>


// The NTB-16 codec: the parameter structure, the builder, the parser and the rules between them. The expected bytes are written by hand
// from the NCM 1.0 layout, and each malformed block breaks one rule. The fuzz loop uses a fixed seed, so a failure repeats.
namespace {


using Bytes = std::vector<uint8>;


struct Datagram {
	uint32 offset = 0;
	Bytes bytes;
};


// A frame whose byte at position i is seed + 7 * i, so two frames of different seeds never match by accident.
Bytes MakeFrame(uint32 size, uint8 seed) {
	Bytes frame(size);
	for (uint32 index = 0; index < size; index++) {
		frame[index] = static_cast<uint8>(seed + index * 7);
	}

	return frame;
}


// A 64-bit xorshift. The state must start non-zero.
uint64 Next(uint64& state) {
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}


// Compares actual with expected byte for byte. A mismatch prints where and both lengths.
void ExpectBytes(std::span<const uint8> actual, std::span<const uint8> expected, const char* file, int line) {
	bool same = actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin());
	if (!same) {
		SimTest::failureCount++;
		std::fprintf(stderr, "%s:%d: bytes differ (actual %zu bytes, expected %zu)\n", file, line, actual.size(), expected.size());
	}
}


#define SIM_EXPECT_BYTES(actual, expected) ExpectBytes((actual), (expected), __FILE__, __LINE__)


// Parses input and keeps every datagram with its offset in the block. Returns the parser's status.
USBNcmNtb16Status Collect(std::span<const uint8> input, const USBNcmNtbLimits& limits, std::vector<Datagram>& out) {
	out.clear();
	const uint8* base = input.data();
	auto store = [&out, base](std::span<const uint8> datagram) {
		Datagram item;
		item.offset = static_cast<uint32>(datagram.data() - base);
		item.bytes.assign(datagram.begin(), datagram.end());
		out.push_back(item);
	};
	return USBNcmNtb16ForEachDatagram(input, limits, store);
}


// A block that breaks the rule the status names must be rejected by both the check and the delivery, and deliver no datagram.
void ExpectRejected(std::span<const uint8> input, const USBNcmNtbLimits& limits, USBNcmNtb16Status expected, const char* file, int line) {
	uint32 delivered = 0;
	auto count = [&delivered](std::span<const uint8>) {
		delivered++;
	};
	USBNcmNtb16Status actual = USBNcmNtb16ForEachDatagram(input, limits, count);
	bool matches = actual == expected && delivered == 0 && USBNcmNtb16Check(input, limits) == expected;
	if (!matches) {
		std::fprintf(stderr, "%s:%d: status %d, expected %d, %u datagram(s) delivered\n", file, line, static_cast<int>(actual), static_cast<int>(expected), delivered);
		SimTest::failureCount++;
	}
}


#define SIM_EXPECT_REJECTED(input, limits, expected) ExpectRejected((input), (limits), (expected), __FILE__, __LINE__)


// The block of one 14-byte datagram at offset 12, the NDP at 28 with one pair and the terminator, 44 bytes in all. The datagram is
// 0xAA throughout. Its NDP is at 28 and its pair at 36.
Bytes BaseOneDatagram() {
	return {
		0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x1C, 0x00,
		0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
		0x00, 0x00,
		0x4E, 0x43, 0x4D, 0x30, 0x10, 0x00, 0x00, 0x00,
		0x0C, 0x00, 0x0E, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
}


// Two datagrams of 14 bytes: 0xAA at 12 and 0xBB at 28. The NDP at 44 lists both, 64 bytes in all.
Bytes BaseTwoDatagrams() {
	return {
		0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x40, 0x00, 0x2C, 0x00,
		0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
		0x00, 0x00,
		0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
		0x00, 0x00,
		0x4E, 0x43, 0x4D, 0x30, 0x14, 0x00, 0x00, 0x00,
		0x0C, 0x00, 0x0E, 0x00,
		0x1C, 0x00, 0x0E, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
}


// Two NDPs chained: the NDP at 44 lists the datagram at 12 and points at the NDP at 60, which lists the one at 28. 76 bytes in all.
Bytes BaseChain() {
	return {
		0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x4C, 0x00, 0x2C, 0x00,
		0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
		0x00, 0x00,
		0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
		0x00, 0x00,
		0x4E, 0x43, 0x4D, 0x30, 0x10, 0x00, 0x3C, 0x00,
		0x0C, 0x00, 0x0E, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x4E, 0x43, 0x4D, 0x30, 0x10, 0x00, 0x00, 0x00,
		0x1C, 0x00, 0x0E, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
}


void TestDefaultParameterBytes() {
	USBNcmNtbParameters parameters;
	constexpr std::array<uint8, 28> expected = {
		0x1C, 0x00,
		0x01, 0x00,
		0x00, 0x08, 0x00, 0x00,
		0x04, 0x00,
		0x00, 0x00,
		0x04, 0x00,
		0x00, 0x00,
		0x00, 0x08, 0x00, 0x00,
		0x04, 0x00,
		0x00, 0x00,
		0x04, 0x00,
		0x00, 0x00,
	};
	std::array<uint8, 28> actual = USBNcmEncodeNtbParameters(parameters);
	SIM_EXPECT_BYTES(std::span<const uint8>(actual), expected);
}


void TestCustomParameterBytes() {
	USBNcmNtbParameters parameters;
	parameters.formatsSupported = 0x0003;
	parameters.inMaxSize = 16384;
	parameters.ndpInDivisor = 8;
	parameters.ndpInPayloadRemainder = 2;
	parameters.ndpInAlignment = 16;
	parameters.outMaxDatagrams = 0x0105;
	constexpr std::array<uint8, 28> expected = {
		0x1C, 0x00,
		0x03, 0x00,
		0x00, 0x40, 0x00, 0x00,
		0x08, 0x00,
		0x02, 0x00,
		0x10, 0x00,
		0x00, 0x00,
		0x00, 0x08, 0x00, 0x00,
		0x04, 0x00,
		0x00, 0x00,
		0x04, 0x00,
		0x05, 0x01,
	};
	std::array<uint8, 28> actual = USBNcmEncodeNtbParameters(parameters);
	SIM_EXPECT_BYTES(std::span<const uint8>(actual), expected);
}


void TestParameterValidation() {
	USBNcmNtbParameters defaults;
	SIM_EXPECT(USBNcmNtbParametersValid(defaults));

	USBNcmNtbParameters odd;
	odd.ndpInDivisor = 3;
	SIM_EXPECT(!USBNcmNtbParametersValid(odd));

	USBNcmNtbParameters remainderTooLarge;
	remainderTooLarge.ndpInPayloadRemainder = 4;
	SIM_EXPECT(!USBNcmNtbParametersValid(remainderTooLarge));

	USBNcmNtbParameters smallAlignment;
	smallAlignment.ndpOutAlignment = 2;
	SIM_EXPECT(!USBNcmNtbParametersValid(smallAlignment));

	USBNcmNtbParameters smallInput;
	smallInput.inMaxSize = 1024;
	SIM_EXPECT(!USBNcmNtbParametersValid(smallInput));

	USBNcmNtbParameters remainderBelowDivisor;
	remainderBelowDivisor.ndpInDivisor = 8;
	remainderBelowDivisor.ndpInPayloadRemainder = 7;
	SIM_EXPECT(USBNcmNtbParametersValid(remainderBelowDivisor));
}


// Two 60-byte datagrams at the default layout: the NTH16 at 0, the datagrams at 12 and 72, the NDP at 132 (20 bytes), 152 bytes in all.
void TestTwoDatagramBlockBytes() {
	Bytes storage(2048, 0xEE);
	USBNcmNtb16Builder<> builder(storage, USBNcmNtbParameters());
	SIM_EXPECT(builder.Reset(USBNcmNtbLimits()));
	Bytes first = MakeFrame(60, 0x10);
	Bytes second = MakeFrame(60, 0x20);
	SIM_EXPECT(builder.TryAdd(first));
	SIM_EXPECT(builder.TryAdd(second));
	uint32 length = builder.Finish();
	SIM_EXPECT_EQ(length, 152);

	std::span<const uint8> block = std::span<const uint8>(storage).first(length);
	constexpr std::array<uint8, 12> header = {0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x98, 0x00, 0x84, 0x00};
	constexpr std::array<uint8, 20> ndp = {
		0x4E, 0x43, 0x4D, 0x30,
		0x14, 0x00,
		0x00, 0x00,
		0x0C, 0x00, 0x3C, 0x00,
		0x48, 0x00, 0x3C, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	SIM_EXPECT_BYTES(block.first(12), header);
	SIM_EXPECT_BYTES(block.subspan(12, 60), first);
	SIM_EXPECT_BYTES(block.subspan(72, 60), second);
	SIM_EXPECT_BYTES(block.subspan(132, 20), ndp);

	std::vector<Datagram> parsed;
	SIM_EXPECT(Collect(block, USBNcmNtbLimits(), parsed) == USBNcmNtb16Status::ok);
	SIM_EXPECT_EQ(parsed.size(), 2);
	if (parsed.size() == 2) {
		SIM_EXPECT_EQ(parsed[0].offset, 12);
		SIM_EXPECT_EQ(parsed[1].offset, 72);
		SIM_EXPECT(parsed[0].bytes == first);
		SIM_EXPECT(parsed[1].bytes == second);
	}
}


// Divisor 4 with remainder 2 puts each datagram at an offset of 2 modulo 4: 14, 30 and 46. The NDP alignment of 16 puts the NDP at 64.
// The bytes 12 and 13, between the header and the first datagram, are padding and are zero, even though the buffer held 0xEE.
void TestAlignmentAndPadding() {
	USBNcmNtbParameters parameters;
	parameters.ndpInDivisor = 4;
	parameters.ndpInPayloadRemainder = 2;
	parameters.ndpInAlignment = 16;
	Bytes storage(2048, 0xEE);
	USBNcmNtb16Builder<> builder(storage, parameters);
	SIM_EXPECT(builder.Reset(USBNcmNtbLimits()));
	Bytes first = MakeFrame(14, 0x30);
	Bytes second = MakeFrame(14, 0x40);
	Bytes third = MakeFrame(14, 0x50);
	SIM_EXPECT(builder.TryAdd(first));
	SIM_EXPECT(builder.TryAdd(second));
	SIM_EXPECT(builder.TryAdd(third));
	uint32 length = builder.Finish();
	SIM_EXPECT_EQ(length, 88);

	std::span<const uint8> block = std::span<const uint8>(storage).first(length);
	constexpr std::array<uint8, 12> header = {0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x58, 0x00, 0x40, 0x00};
	constexpr std::array<uint8, 2> padding = {0x00, 0x00};
	constexpr std::array<uint8, 24> ndp = {
		0x4E, 0x43, 0x4D, 0x30,
		0x18, 0x00,
		0x00, 0x00,
		0x0E, 0x00, 0x0E, 0x00,
		0x1E, 0x00, 0x0E, 0x00,
		0x2E, 0x00, 0x0E, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	SIM_EXPECT_BYTES(block.first(12), header);
	SIM_EXPECT_BYTES(block.subspan(12, 2), padding);
	SIM_EXPECT_BYTES(block.subspan(14, 14), first);
	SIM_EXPECT_BYTES(block.subspan(28, 2), padding);
	SIM_EXPECT_BYTES(block.subspan(30, 14), second);
	SIM_EXPECT_BYTES(block.subspan(44, 2), padding);
	SIM_EXPECT_BYTES(block.subspan(46, 14), third);
	constexpr std::array<uint8, 4> zeros = {0x00, 0x00, 0x00, 0x00};
	SIM_EXPECT_BYTES(block.subspan(60, 4), zeros);
	SIM_EXPECT_BYTES(block.subspan(64, 24), ndp);

	std::vector<Datagram> parsed;
	SIM_EXPECT(Collect(block, USBNcmNtbLimits(), parsed) == USBNcmNtb16Status::ok);
	SIM_EXPECT_EQ(parsed.size(), 3);
	if (parsed.size() == 3) {
		SIM_EXPECT_EQ(parsed[0].offset, 14);
		SIM_EXPECT_EQ(parsed[1].offset, 30);
		SIM_EXPECT_EQ(parsed[2].offset, 46);
	}
}


// A block limit of 100 bytes takes the first 60-byte datagram (88 bytes with its NDP) and refuses the second (152 bytes), with nothing
// written. The same happens when the buffer is 100 bytes and the host's limit is the default. A refused datagram leaves the buffer as it was.
void TestInputSizeLimit() {
	Bytes storage(2048, 0xEE);
	USBNcmNtb16Builder<> builder(storage, USBNcmNtbParameters());
	USBNcmNtbLimits limits;
	limits.maxNtbSize = 100;
	SIM_EXPECT(builder.Reset(limits));
	Bytes first = MakeFrame(60, 0x10);
	Bytes second = MakeFrame(60, 0x20);
	SIM_EXPECT(builder.TryAdd(first));
	SIM_EXPECT(!builder.TryAdd(second));
	uint32 length = builder.Finish();
	SIM_EXPECT_EQ(length, 88);
	SIM_EXPECT_EQ(storage[88], 0xEE);
	// The last byte of the NDP is the high byte of the terminator, which is zero.
	SIM_EXPECT_EQ(storage[87], 0x00);

	Bytes small(100, 0xEE);
	USBNcmNtb16Builder<> smallBuilder(small, USBNcmNtbParameters());
	SIM_EXPECT(smallBuilder.Reset(USBNcmNtbLimits()));
	SIM_EXPECT(smallBuilder.TryAdd(first));
	SIM_EXPECT(!smallBuilder.TryAdd(second));
	SIM_EXPECT_EQ(smallBuilder.Finish(), 88);
}


// The host's limit may not exceed dwNtbInMaxSize, which is 2048 here, so a limit of 65535 is taken as 2048. A 1514-byte datagram fits
// (1544 bytes with its NDP); a second one would need 3044 bytes and is refused.
void TestHostLimitClampedToParameter() {
	Bytes storage(4096, 0xEE);
	USBNcmNtb16Builder<> builder(storage, USBNcmNtbParameters());
	USBNcmNtbLimits limits;
	limits.maxNtbSize = 65535;
	SIM_EXPECT(builder.Reset(limits));
	Bytes big = MakeFrame(1514, 0x60);
	SIM_EXPECT(builder.TryAdd(big));
	SIM_EXPECT(!builder.TryAdd(big));
	SIM_EXPECT_EQ(builder.Finish(), 1544);
}


// The builder keeps room for MaxDatagramsPerNtb entries, and the host's count caps it lower still. A third 14-byte frame is refused both ways.
void TestDatagramCountLimits() {
	Bytes storage(2048, 0xEE);
	Bytes frame = MakeFrame(14, 0x01);

	USBNcmNtb16Builder<2> fixedRoom(storage, USBNcmNtbParameters());
	SIM_EXPECT(fixedRoom.Reset(USBNcmNtbLimits()));
	SIM_EXPECT(fixedRoom.TryAdd(frame));
	SIM_EXPECT(fixedRoom.TryAdd(frame));
	SIM_EXPECT(!fixedRoom.TryAdd(frame));
	// Two 14-byte frames at 12 and 28, the NDP at 44 with three pairs: 44 + 20.
	SIM_EXPECT_EQ(fixedRoom.Finish(), 64);

	USBNcmNtb16Builder<> hostCount(storage, USBNcmNtbParameters());
	USBNcmNtbLimits limits;
	limits.maxDatagrams = 1;
	SIM_EXPECT(hostCount.Reset(limits));
	SIM_EXPECT(hostCount.TryAdd(frame));
	SIM_EXPECT(!hostCount.TryAdd(frame));
	// One 14-byte frame at 12, the NDP at 28 with two pairs: 28 + 16.
	SIM_EXPECT_EQ(hostCount.Finish(), 44);
}


void TestDatagramSizeLimits() {
	Bytes storage(2048, 0xEE);
	USBNcmNtb16Builder<> builder(storage, USBNcmNtbParameters());
	SIM_EXPECT(builder.Reset(USBNcmNtbLimits()));
	SIM_EXPECT(!builder.TryAdd(MakeFrame(13, 0x01)));
	SIM_EXPECT(!builder.TryAdd(MakeFrame(1515, 0x01)));
	SIM_EXPECT(builder.TryAdd(MakeFrame(14, 0x01)));
	SIM_EXPECT(builder.TryAdd(MakeFrame(1514, 0x02)));
	// The 14-byte frame ends at 26, the 1514-byte one starts at 28 and ends at 1542, the NDP is at 1544 and lists two pairs and the
	// terminator: 1544 + 20.
	SIM_EXPECT_EQ(builder.Finish(), 1564);

	USBNcmNtb16Builder<> capped(storage, USBNcmNtbParameters());
	USBNcmNtbLimits limits;
	limits.maxDatagramSize = 100;
	SIM_EXPECT(capped.Reset(limits));
	SIM_EXPECT(!capped.TryAdd(MakeFrame(101, 0x03)));
	SIM_EXPECT(capped.TryAdd(MakeFrame(100, 0x03)));
}


// A frame the builder refuses writes nothing: the block is the one it was before the refusal, and the byte after the block is untouched.
// The 1514-byte frame would need 1608 bytes, so the limit of 100 refuses it.
void TestRefusedFrameWritesNothing() {
	Bytes storage(2048, 0xEE);
	USBNcmNtb16Builder<> builder(storage, USBNcmNtbParameters());
	USBNcmNtbLimits limits;
	limits.maxNtbSize = 100;
	SIM_EXPECT(builder.Reset(limits));
	Bytes first = MakeFrame(60, 0x10);
	SIM_EXPECT(builder.TryAdd(first));
	SIM_EXPECT(!builder.TryAdd(MakeFrame(1514, 0x20)));
	SIM_EXPECT_EQ(builder.Finish(), 88);
	SIM_EXPECT_EQ(storage[88], 0xEE);
	SIM_EXPECT_BYTES(std::span<const uint8>(storage).subspan(12, 60), first);
}


void TestEmptyBlockAndUnstartedBuilder() {
	Bytes storage(2048, 0xEE);
	USBNcmNtb16Builder<> builder(storage, USBNcmNtbParameters());
	SIM_EXPECT(!builder.TryAdd(MakeFrame(14, 0x01)));
	SIM_EXPECT_EQ(builder.Finish(), 0);

	SIM_EXPECT(builder.Reset(USBNcmNtbLimits()));
	SIM_EXPECT_EQ(builder.Finish(), 0);
	SIM_EXPECT(!builder.TryAdd(MakeFrame(14, 0x01)));
	SIM_EXPECT_EQ(storage[0], 0xEE);
}


void TestResetRefusals() {
	Bytes storage(2048, 0xEE);
	USBNcmNtbParameters odd;
	odd.ndpInDivisor = 3;
	USBNcmNtb16Builder<> badParameters(storage, odd);
	SIM_EXPECT(!badParameters.Reset(USBNcmNtbLimits()));
	SIM_EXPECT(!badParameters.TryAdd(MakeFrame(14, 0x01)));

	Bytes tooSmall(27, 0xEE);
	USBNcmNtb16Builder<> smallBuffer(tooSmall, USBNcmNtbParameters());
	SIM_EXPECT(!smallBuffer.Reset(USBNcmNtbLimits()));

	// A 28-byte buffer holds the headers of an empty block, but no datagram with its NDP entry (12 + 14 + 16 is 42).
	Bytes exact(28, 0xEE);
	USBNcmNtb16Builder<> headersOnly(exact, USBNcmNtbParameters());
	SIM_EXPECT(headersOnly.Reset(USBNcmNtbLimits()));
	SIM_EXPECT(!headersOnly.TryAdd(MakeFrame(14, 0x01)));
	SIM_EXPECT_EQ(headersOnly.Finish(), 0);
}


// The sequence number of block n is n modulo 65536, starting at 0. Each block is 44 bytes: NTH 12, one 14-byte datagram, NDP 16.
void TestSequenceWrap() {
	Bytes storage(64, 0xEE);
	USBNcmNtb16Builder<> builder(storage, USBNcmNtbParameters());
	Bytes frame = MakeFrame(14, 0x01);
	for (uint32 index = 0; index <= 65536; index++) {
		SIM_EXPECT(builder.Reset(USBNcmNtbLimits()));
		SIM_EXPECT(builder.TryAdd(frame));
		uint32 length = builder.Finish();
		SIM_EXPECT_EQ(length, 44);
		SIM_EXPECT_EQ(USBNcmRead16(storage, 6), index % 65536);
	}
}


// A valid block with one 14-byte datagram: every rule holds, and the datagram is the one at 12.
void TestValidBlocks() {
	std::vector<Datagram> parsed;
	Bytes one = BaseOneDatagram();
	SIM_EXPECT(Collect(one, USBNcmNtbLimits(), parsed) == USBNcmNtb16Status::ok);
	SIM_EXPECT_EQ(parsed.size(), 1);
	if (parsed.size() == 1) {
		SIM_EXPECT_EQ(parsed[0].offset, 12);
		SIM_EXPECT_EQ(parsed[0].bytes.size(), 14);
	}

	// Bytes after wBlockLength are padding the host may add, and the block is accepted.
	Bytes padded = one;
	padded.insert(padded.end(), 20, 0x00);
	SIM_EXPECT(Collect(padded, USBNcmNtbLimits(), parsed) == USBNcmNtb16Status::ok);
	SIM_EXPECT_EQ(parsed.size(), 1);

	// The limits are inclusive: a block of exactly maxNtbSize bytes, a datagram of exactly the minimum size and one allowed datagram.
	USBNcmNtbLimits exact;
	exact.maxNtbSize = 44;
	exact.maxDatagrams = 1;
	exact.maxDatagramSize = 14;
	SIM_EXPECT(Collect(one, exact, parsed) == USBNcmNtb16Status::ok);
	SIM_EXPECT_EQ(parsed.size(), 1);

	Bytes two = BaseTwoDatagrams();
	SIM_EXPECT(Collect(two, USBNcmNtbLimits(), parsed) == USBNcmNtb16Status::ok);
	SIM_EXPECT_EQ(parsed.size(), 2);
	if (parsed.size() == 2) {
		SIM_EXPECT_EQ(parsed[0].offset, 12);
		SIM_EXPECT_EQ(parsed[1].offset, 28);
		SIM_EXPECT(parsed[1].bytes == Bytes(14, static_cast<uint8>(0xBB)));
	}
}


// Entries after the first (0, 0) pair of an NDP are not read: the second datagram below the terminator is never delivered.
void TestEntriesAfterTerminatorIgnored() {
	// The second pair becomes the terminator, and a valid pair (28, 14) sits after it, where it must not be read.
	Bytes block = BaseTwoDatagrams();
	block[56] = 0x00;
	block[57] = 0x00;
	block[58] = 0x00;
	block[59] = 0x00;
	block[60] = 0x1C;
	block[61] = 0x00;
	block[62] = 0x0E;
	block[63] = 0x00;
	std::vector<Datagram> parsed;
	SIM_EXPECT(Collect(block, USBNcmNtbLimits(), parsed) == USBNcmNtb16Status::ok);
	SIM_EXPECT_EQ(parsed.size(), 1);
}


// Two NDPs in a chain deliver both datagrams, in the order the chain lists them.
void TestNdpChain() {
	std::vector<Datagram> parsed;
	Bytes chain = BaseChain();
	SIM_EXPECT(Collect(chain, USBNcmNtbLimits(), parsed) == USBNcmNtb16Status::ok);
	SIM_EXPECT_EQ(parsed.size(), 2);
	if (parsed.size() == 2) {
		SIM_EXPECT_EQ(parsed[0].offset, 12);
		SIM_EXPECT_EQ(parsed[1].offset, 28);
		SIM_EXPECT_EQ(parsed[1].bytes.size(), 14);
	}
}


// The malformed blocks: each breaks one rule of the parser, and each is rejected with that rule's status.
void TestMalformedBlocks() {
	USBNcmNtbLimits limits;

	Bytes full = BaseOneDatagram();
	Bytes truncated(full.begin(), full.begin() + 11);
	SIM_EXPECT_REJECTED(truncated, limits, USBNcmNtb16Status::headerTooShort);

	Bytes signature = BaseOneDatagram();
	signature[0] = 0x00;
	SIM_EXPECT_REJECTED(signature, limits, USBNcmNtb16Status::badSignature);

	Bytes headerLength = BaseOneDatagram();
	headerLength[4] = 0x10;
	SIM_EXPECT_REJECTED(headerLength, limits, USBNcmNtb16Status::badHeaderLength);

	Bytes zeroBlock = BaseOneDatagram();
	zeroBlock[8] = 0x00;
	zeroBlock[9] = 0x00;
	SIM_EXPECT_REJECTED(zeroBlock, limits, USBNcmNtb16Status::blockTooSmall);

	Bytes elevenBlock = BaseOneDatagram();
	elevenBlock[8] = 0x0B;
	elevenBlock[9] = 0x00;
	SIM_EXPECT_REJECTED(elevenBlock, limits, USBNcmNtb16Status::blockTooSmall);

	Bytes longBlock = BaseOneDatagram();
	longBlock[8] = 0x2D;
	SIM_EXPECT_REJECTED(longBlock, limits, USBNcmNtb16Status::blockTooLarge);

	USBNcmNtbLimits smallOut;
	smallOut.maxNtbSize = 40;
	SIM_EXPECT_REJECTED(BaseOneDatagram(), smallOut, USBNcmNtb16Status::blockTooLarge);

	Bytes ndpZero = BaseOneDatagram();
	ndpZero[10] = 0x00;
	ndpZero[11] = 0x00;
	SIM_EXPECT_REJECTED(ndpZero, limits, USBNcmNtb16Status::ndpIndexInvalid);

	Bytes ndpMisaligned = BaseOneDatagram();
	ndpMisaligned[10] = 0x1E;
	SIM_EXPECT_REJECTED(ndpMisaligned, limits, USBNcmNtb16Status::ndpIndexInvalid);

	Bytes ndpNoRoom = BaseOneDatagram();
	ndpNoRoom[10] = 0x2C;
	SIM_EXPECT_REJECTED(ndpNoRoom, limits, USBNcmNtb16Status::ndpIndexInvalid);

	Bytes ndpSig = BaseOneDatagram();
	ndpSig[28] = 0x00;
	SIM_EXPECT_REJECTED(ndpSig, limits, USBNcmNtb16Status::ndpSignature);

	Bytes crc = BaseOneDatagram();
	crc[31] = 0x31;
	SIM_EXPECT_REJECTED(crc, limits, USBNcmNtb16Status::ndpCrcUnsupported);

	Bytes ndpShort = BaseOneDatagram();
	ndpShort[32] = 0x0C;
	SIM_EXPECT_REJECTED(ndpShort, limits, USBNcmNtb16Status::ndpLengthInvalid);

	Bytes ndpOdd = BaseOneDatagram();
	ndpOdd[32] = 0x12;
	SIM_EXPECT_REJECTED(ndpOdd, limits, USBNcmNtb16Status::ndpLengthInvalid);

	Bytes ndpPast = BaseOneDatagram();
	ndpPast[32] = 0x14;
	SIM_EXPECT_REJECTED(ndpPast, limits, USBNcmNtb16Status::ndpLengthInvalid);

	// The second pair sits where the terminator should be, so the (0, 0) pair is missing from inside wLength.
	Bytes unterminated = BaseOneDatagram();
	unterminated[40] = 0x0C;
	unterminated[41] = 0x00;
	unterminated[42] = 0x0E;
	unterminated[43] = 0x00;
	SIM_EXPECT_REJECTED(unterminated, limits, USBNcmNtb16Status::ndpUnterminated);

	Bytes datagramLow = BaseOneDatagram();
	datagramLow[36] = 0x08;
	SIM_EXPECT_REJECTED(datagramLow, limits, USBNcmNtb16Status::datagramOutsideBlock);

	Bytes datagramLong = BaseOneDatagram();
	datagramLong[38] = 0x21;
	SIM_EXPECT_REJECTED(datagramLong, limits, USBNcmNtb16Status::datagramOutsideBlock);

	Bytes datagramSmall = BaseOneDatagram();
	datagramSmall[38] = 0x0D;
	SIM_EXPECT_REJECTED(datagramSmall, limits, USBNcmNtb16Status::datagramSizeInvalid);

	USBNcmNtbLimits narrow;
	narrow.maxDatagramSize = 13;
	SIM_EXPECT_REJECTED(BaseOneDatagram(), narrow, USBNcmNtb16Status::datagramSizeInvalid);

	USBNcmNtbLimits oneDatagram;
	oneDatagram.maxDatagrams = 1;
	SIM_EXPECT_REJECTED(BaseTwoDatagrams(), oneDatagram, USBNcmNtb16Status::tooManyDatagrams);

	// The NDP points at itself: the walk reads the same NDP until it runs out of NDPs the block could hold.
	Bytes cycle = BaseOneDatagram();
	cycle[34] = 0x1C;
	SIM_EXPECT_REJECTED(cycle, limits, USBNcmNtb16Status::ndpChainTooLong);

	Bytes chainMisaligned = BaseOneDatagram();
	chainMisaligned[34] = 0x1E;
	SIM_EXPECT_REJECTED(chainMisaligned, limits, USBNcmNtb16Status::ndpIndexInvalid);

	Bytes chainBroken = BaseChain();
	chainBroken[66] = 0x1E;
	SIM_EXPECT_REJECTED(chainBroken, limits, USBNcmNtb16Status::ndpIndexInvalid);
}


// A block of 12 to 15 bytes is a header with no room for an NDP: the NDP at 12 needs 8 bytes of NDP header inside the block. The rule is
// the NDP index, not a chain that is too long: the block holds no NDP at all, so no chain can be in it.
void TestShortBlocksRefuseNdpIndex() {
	USBNcmNtbLimits limits;
	for (uint8 length = 12; length <= 15; length++) {
		Bytes block = {
			0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, length, 0x00, 0x0C, 0x00,
		};
		block.resize(length, 0x00);
		SIM_EXPECT_REJECTED(block, limits, USBNcmNtb16Status::ndpIndexInvalid);
	}
}


// The fuzz loop: random blocks and mutations of valid ones. Every datagram delivered lies inside the block the header declares, is no
// shorter than an Ethernet header and no longer than the limit, and the check and the delivery give the same status.
void TestSeededFuzz() {
	uint64 state = 0x2545F4914F6CDD1DULL;
	std::array<Bytes, 3> seeds = {BaseOneDatagram(), BaseTwoDatagrams(), BaseChain()};
	std::array<uint32, 6> blockLimits = {12, 44, 64, 100, 2048, 65535};
	uint32 accepted = 0;
	uint32 rejected = 0;

	for (uint32 iteration = 0; iteration < 20000; iteration++) {
		Bytes input;
		uint32 mode = static_cast<uint32>(Next(state) % 3);
		if (mode == 0) {
			uint32 length = static_cast<uint32>(Next(state) % 96);
			input.resize(length);
			for (uint32 index = 0; index < length; index++) {
				input[index] = static_cast<uint8>(Next(state));
			}

			if (length >= 12 && Next(state) % 2 == 0) {
				std::copy(USBNcmNth16Signature.begin(), USBNcmNth16Signature.end(), input.begin());
				input[4] = 0x0C;
				input[5] = 0x00;
				input[8] = static_cast<uint8>(length);
				input[9] = 0x00;
				input[10] = static_cast<uint8>(Next(state) % (length + 1));
				input[11] = 0x00;
			}
		} else {
			input = seeds[Next(state) % seeds.size()];
			uint32 flips = 1 + static_cast<uint32>(Next(state) % 4);
			for (uint32 flip = 0; flip < flips; flip++) {
				uint32 position = static_cast<uint32>(Next(state) % input.size());
				input[position] = static_cast<uint8>(Next(state));
			}

			if (Next(state) % 3 == 0) {
				input.resize(static_cast<std::size_t>(Next(state) % (input.size() + 1)));
			}
		}

		USBNcmNtbLimits limits;
		limits.maxNtbSize = blockLimits[Next(state) % blockLimits.size()];
		limits.maxDatagrams = Next(state) % 3 == 0 ? 0 : static_cast<uint32>(Next(state) % 4);
		limits.maxDatagramSize = 14 + static_cast<uint32>(Next(state) % 1501);

		std::span<const uint8> view(input);
		uint32 blockLength = input.size() >= 10 ? USBNcmRead16(view, 8) : 0;
		uint32 delivered = 0;
		auto check = [&](std::span<const uint8> datagram) {
			delivered++;
			uint32 offset = static_cast<uint32>(datagram.data() - input.data());
			SimTest::Expect(offset >= 12 && offset + datagram.size() <= blockLength, "datagram lies inside its block", __FILE__, __LINE__);
			SimTest::Expect(datagram.size() >= 14 && datagram.size() <= limits.maxDatagramSize, "datagram size within limits", __FILE__, __LINE__);
		};
		USBNcmNtb16Status status = USBNcmNtb16ForEachDatagram(view, limits, check);
		SimTest::Expect(status == USBNcmNtb16Check(view, limits), "check and delivery agree", __FILE__, __LINE__);
		if (status == USBNcmNtb16Status::ok) {
			accepted++;
			SimTest::Expect(limits.maxDatagrams == 0 || delivered <= limits.maxDatagrams, "datagram count within limit", __FILE__, __LINE__);
		} else {
			rejected++;
			SimTest::Expect(delivered == 0, "a rejected block delivers nothing", __FILE__, __LINE__);
		}
	}

	SimTest::Expect(accepted > 0, "the fuzz loop accepts some blocks", __FILE__, __LINE__);
	SimTest::Expect(rejected > 0, "the fuzz loop rejects some blocks", __FILE__, __LINE__);
}


// Builder then parser over random layouts: random divisors, remainders and alignments, random frame sizes, and some frames the builder
// refuses. The parser must return exactly the accepted frames, in order, each at an offset the divisor and remainder allow.
void TestRandomRoundTrip() {
	uint64 state = 0x9E3779B97F4A7C15ULL;
	std::array<uint32, 3> inputSizes = {2048, 4096, 16384};
	for (uint32 round = 0; round < 300; round++) {
		USBNcmNtbParameters parameters;
		uint32 divisor = 4u << (Next(state) % 3);
		parameters.ndpInDivisor = static_cast<uint16>(divisor);
		parameters.ndpInPayloadRemainder = static_cast<uint16>(Next(state) % divisor);
		uint32 alignment = 4u << (Next(state) % 4);
		parameters.ndpInAlignment = static_cast<uint16>(alignment);
		parameters.inMaxSize = inputSizes[Next(state) % inputSizes.size()];

		USBNcmNtbLimits limits;
		limits.maxNtbSize = parameters.inMaxSize;
		limits.maxDatagrams = Next(state) % 2 == 0 ? 0 : static_cast<uint32>(1 + Next(state) % 40);

		Bytes storage(16384, 0xEE);
		USBNcmNtb16Builder<> builder(storage, parameters);
		SimTest::Expect(builder.Reset(limits), "builder starts a block", __FILE__, __LINE__);
		std::vector<Bytes> sent;
		uint32 attempts = 1 + static_cast<uint32>(Next(state) % 60);
		for (uint32 attempt = 0; attempt < attempts; attempt++) {
			uint32 size = 14 + static_cast<uint32>(Next(state) % 1501);
			Bytes frame = MakeFrame(size, static_cast<uint8>(Next(state)));
			if (builder.TryAdd(frame)) {
				sent.push_back(frame);
			}
		}

		uint32 length = builder.Finish();
		if (sent.empty()) {
			SIM_EXPECT_EQ(length, 0);
			continue;
		}

		SIM_EXPECT(length <= limits.maxNtbSize);
		std::vector<Datagram> parsed;
		SIM_EXPECT(Collect(std::span<const uint8>(storage).first(length), limits, parsed) == USBNcmNtb16Status::ok);
		SIM_EXPECT_EQ(parsed.size(), sent.size());
		std::size_t common = std::min(parsed.size(), sent.size());
		for (std::size_t index = 0; index < common; index++) {
			SIM_EXPECT(parsed[index].bytes == sent[index]);
			SIM_EXPECT_EQ(parsed[index].offset % divisor, parameters.ndpInPayloadRemainder);
		}

		uint32 ndpOffset = USBNcmRead16(storage, 10);
		SIM_EXPECT_EQ(ndpOffset % alignment, 0);
	}
}


}


int main() {
	TestDefaultParameterBytes();
	TestCustomParameterBytes();
	TestParameterValidation();
	TestTwoDatagramBlockBytes();
	TestAlignmentAndPadding();
	TestInputSizeLimit();
	TestHostLimitClampedToParameter();
	TestDatagramCountLimits();
	TestDatagramSizeLimits();
	TestRefusedFrameWritesNothing();
	TestEmptyBlockAndUnstartedBuilder();
	TestResetRefusals();
	TestSequenceWrap();
	TestValidBlocks();
	TestEntriesAfterTerminatorIgnored();
	TestNdpChain();
	TestMalformedBlocks();
	TestShortBlocksRefuseNdpIndex();
	TestSeededFuzz();
	TestRandomRoundTrip();
	return SimTest::Finish("TestNcmNtb16");
}
