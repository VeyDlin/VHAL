// Vectors written by hand from the paper (Cheshire and Baker) and the Wikipedia article, the exhaustive small packets, and random packets
// checked against a reference encoder that is written from the block rules and shares no code with the codec.
#include <Utilities/Encoding/COBS/Cobs.h>
#include <cstddef>
#include <cstdio>
#include <random>
#include <vector>

using Encoding::Cobs;

static int failures = 0;

static void Check(bool condition, const char* what) {
	if (!condition) {
		std::printf("FAILED: %s\n", what);
		failures++;
	}
}


// Reference encoder. A block is a run of up to 254 non-zero bytes, followed by a zero when the run ends at one. A run of 254 bytes has code
// 0xFF and no zero; when it ends the packet, no code follows it. The last block of a packet is always written, even when it is empty.
static std::vector<uint8> ReferenceEncode(const std::vector<uint8>& in) {
	std::vector<uint8> out;
	std::size_t pos = 0;
	std::size_t n = in.size();
	while (true) {
		std::size_t len = 0;
		while (pos + len < n && len < 254 && in[pos + len] != 0) {
			len++;
		}

		if (len == 254) {
			out.push_back(0xFF);
			out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(pos + len));
			pos += len;
			if (pos == n) {
				break;
			}
			continue;
		}

		out.push_back(static_cast<uint8>(len + 1));
		out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(pos + len));
		pos += len;
		if (pos == n) {
			break;
		}
		pos += 1;
	}
	out.push_back(0x00);
	return out;
}


static std::vector<uint8> EncodeVector(const std::vector<uint8>& in) {
	std::vector<uint8> frame(Cobs::EncodedSize(in.size()));
	Result<std::size_t> result = Cobs::Encode(std::span<const uint8>(in), std::span<uint8>(frame));
	if (result.IsErr()) {
		Check(false, "Encode of a vector succeeds");
		return {};
	}
	frame.resize(result.Value());
	return frame;
}


static void CheckVector(const std::vector<uint8>& packet, const std::vector<uint8>& expected, const char* what) {
	std::vector<uint8> frame = EncodeVector(packet);
	Check(frame == expected, what);
	Check(ReferenceEncode(packet) == expected, what);
}


static void CheckRoundTrip(const std::vector<uint8>& packet, const char* what) {
	std::vector<uint8> frame = EncodeVector(packet);
	std::vector<uint8> inPlace = frame;
	Result<std::size_t> inPlaceResult = Cobs::Decode(std::span<uint8>(inPlace));
	Check(inPlaceResult.IsOk() && inPlaceResult.Value() == packet.size(), what);
	if (inPlaceResult.IsOk()) {
		inPlace.resize(inPlaceResult.Value());
		Check(inPlace == packet, what);
	}

	std::vector<uint8> out(packet.size());
	Result<std::size_t> copyResult = Cobs::Decode(std::span<const uint8>(frame), std::span<uint8>(out));
	Check(copyResult.IsOk() && copyResult.Value() == packet.size(), what);
	Check(out == packet, what);
}


static void TestPaperAndWikipediaVectors() {
	CheckVector({}, { 0x01, 0x00 }, "empty packet");
	CheckVector({ 0x00 }, { 0x01, 0x01, 0x00 }, "one zero");
	CheckVector({ 0x00, 0x00 }, { 0x01, 0x01, 0x01, 0x00 }, "two zeros");
	CheckVector({ 0x00, 0x11, 0x22, 0x33 }, { 0x01, 0x04, 0x11, 0x22, 0x33, 0x00 }, "zero first");
	CheckVector({ 0x11, 0x22, 0x00, 0x33 }, { 0x03, 0x11, 0x22, 0x02, 0x33, 0x00 }, "zero in the middle");
	CheckVector({ 0x11, 0x22, 0x33, 0x44 }, { 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 }, "no zero");
	CheckVector({ 0x11, 0x00, 0x00, 0x00 }, { 0x02, 0x11, 0x01, 0x01, 0x01, 0x00 }, "zeros at the end");

	std::vector<uint8> run253;
	for (std::size_t i = 1; i <= 253; i++) {
		run253.push_back(static_cast<uint8>(i));
	}
	std::vector<uint8> expected253 = { 0xFE };
	expected253.insert(expected253.end(), run253.begin(), run253.end());
	expected253.push_back(0x00);
	CheckVector(run253, expected253, "253 non-zero bytes: code 0xFE");

	std::vector<uint8> run254;
	for (std::size_t i = 1; i <= 254; i++) {
		run254.push_back(static_cast<uint8>(i));
	}
	std::vector<uint8> expected254 = { 0xFF };
	expected254.insert(expected254.end(), run254.begin(), run254.end());
	expected254.push_back(0x00);
	CheckVector(run254, expected254, "254 non-zero bytes: code 0xFF, no zero after the block");

	std::vector<uint8> run255 = run254;
	run255.push_back(0xFF);
	std::vector<uint8> expected255 = { 0xFF };
	expected255.insert(expected255.end(), run254.begin(), run254.end());
	expected255.push_back(0x02);
	expected255.push_back(0xFF);
	expected255.push_back(0x00);
	CheckVector(run255, expected255, "255 non-zero bytes: a second block of one byte");

	std::vector<uint8> run254Zero = run254;
	run254Zero.push_back(0x00);
	std::vector<uint8> expectedZero = { 0xFF };
	expectedZero.insert(expectedZero.end(), run254.begin(), run254.end());
	expectedZero.push_back(0x01);
	expectedZero.push_back(0x01);
	expectedZero.push_back(0x00);
	CheckVector(run254Zero, expectedZero, "254 non-zero bytes then a zero");

	std::vector<uint8> zeros(254, 0x00);
	std::vector<uint8> expectedZeros(255, 0x01);
	expectedZeros.push_back(0x00);
	CheckVector(zeros, expectedZeros, "254 zeros");
}


static void TestExhaustiveSmall() {
	const uint8 values[3] = { 0x00, 0x01, 0xFF };
	for (std::size_t length = 0; length <= 3; length++) {
		std::size_t count = 1;
		for (std::size_t i = 0; i < length; i++) {
			count *= 3;
		}

		for (std::size_t index = 0; index < count; index++) {
			std::vector<uint8> packet;
			std::size_t rest = index;
			for (std::size_t i = 0; i < length; i++) {
				packet.push_back(values[rest % 3]);
				rest /= 3;
			}

			CheckVector(packet, ReferenceEncode(packet), "small packet encodes as the reference");
			CheckRoundTrip(packet, "small packet round trip");
		}
	}
}


static void TestRandom() {
	std::mt19937 generator(20261125u);
	std::uniform_int_distribution<int> lengthDistribution(0, 1000);
	std::uniform_int_distribution<int> byteDistribution(0, 255);
	std::uniform_int_distribution<int> zeroDistribution(0, 3);
	for (int round = 0; round < 2000; round++) {
		std::size_t length = static_cast<std::size_t>(lengthDistribution(generator));
		std::vector<uint8> packet(length);
		for (std::size_t i = 0; i < length; i++) {
			// About one byte in four is zero, so short blocks and long blocks both occur.
			if (zeroDistribution(generator) == 0) {
				packet[i] = 0x00;
			} else {
				packet[i] = static_cast<uint8>(1 + byteDistribution(generator) % 255);
			}
		}

		std::vector<uint8> frame = EncodeVector(packet);
		Check(frame == ReferenceEncode(packet), "random packet encodes as the reference");
		Check(frame.size() <= Cobs::EncodedSize(packet.size()), "random frame fits EncodedSize");
		CheckRoundTrip(packet, "random packet round trip");
	}
}


static void TestEncodedSizeBound() {
	for (std::size_t length = 0; length <= 1000; length++) {
		std::vector<uint8> packet(length, 0xFF);
		std::vector<uint8> zeros(length, 0x00);
		Check(EncodeVector(packet).size() <= Cobs::EncodedSize(length), "EncodedSize bounds non-zero packets");
		Check(EncodeVector(zeros).size() <= Cobs::EncodedSize(length), "EncodedSize bounds zero packets");
	}
}


static void TestEncodeTooSmall() {
	std::vector<uint8> packet = { 0x11, 0x22, 0x00, 0x33 };
	std::vector<uint8> small(6, 0x00);
	Check(Cobs::Encode(std::span<const uint8>(packet), std::span<uint8>(small)).IsOk(), "exact-size output encodes");
	std::vector<uint8> tooSmall(5, 0x00);
	Result<std::size_t> result = Cobs::Encode(std::span<const uint8>(packet), std::span<uint8>(tooSmall));
	Check(result.IsErr() && result.Error() == ResultStatus::messageTooLong, "output one byte short fails with messageTooLong");
}


static void TestDecodeErrors() {
	std::vector<uint8> out(16);
	std::vector<uint8> noDelimiter = { 0x03, 0x11, 0x22 };
	Check(Cobs::Decode(std::span<const uint8>(noDelimiter), std::span<uint8>(out)).IsErr(), "frame without a delimiter fails");
	std::vector<uint8> truncated = { 0x05, 0x11, 0x00 };
	Check(Cobs::Decode(std::span<const uint8>(truncated), std::span<uint8>(out)).IsErr(), "block running into the delimiter fails");
	std::vector<uint8> early = { 0x02, 0x11, 0x00, 0x02, 0x22, 0x00 };
	Check(Cobs::Decode(std::span<const uint8>(early), std::span<uint8>(out)).IsErr(), "delimiter before the end of the input fails");
	std::vector<uint8> empty;
	Check(Cobs::Decode(std::span<const uint8>(empty), std::span<uint8>(out)).IsErr(), "empty input fails");
	std::vector<uint8> validFrame = { 0x03, 0x11, 0x22, 0x00 };
	std::vector<uint8> tiny(1);
	Result<std::size_t> overflow = Cobs::Decode(std::span<const uint8>(validFrame), std::span<uint8>(tiny));
	Check(overflow.IsErr() && overflow.Error() == ResultStatus::bufferOverflow, "packet longer than out fails with bufferOverflow");
}


int main() {
	TestPaperAndWikipediaVectors();
	TestExhaustiveSmall();
	TestRandom();
	TestEncodedSizeBound();
	TestEncodeTooSmall();
	TestDecodeErrors();

	if (failures != 0) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}

	std::printf("TestCobsVectors: all checks passed\n");
	return 0;
}
