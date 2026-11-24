// The decoder object: one byte at a time, into the caller's span, with garbage, resynchronisation, overflow, and constexpr use.
#include <Utilities/Encoding/COBS/Cobs.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <vector>

using Encoding::Cobs;

static int failures = 0;

static void Check(bool condition, const char* what) {
	if (!condition) {
		std::printf("FAILED: %s\n", what);
		failures++;
	}
}


// Feeds bytes to a decoder and returns the packets it completed, in order. Broken and tooLong frames are counted.
struct Feed {
	std::vector<std::vector<uint8>> packets;
	std::size_t broken = 0;
	std::size_t tooLong = 0;
};


static Feed Run(const std::vector<uint8>& stream, std::size_t outSize) {
	Feed feed;
	Cobs::Decoder decoder;
	std::vector<uint8> out(outSize);
	for (uint8 byte : stream) {
		std::size_t length = 0;
		Cobs::Status status = decoder.Push(byte, std::span<uint8>(out), length);
		if (status == Cobs::Status::packet) {
			feed.packets.emplace_back(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(length));
		} else if (status == Cobs::Status::broken) {
			feed.broken++;
		} else if (status == Cobs::Status::tooLong) {
			feed.tooLong++;
		}
	}
	return feed;
}


static void TestByteAtATime() {
	std::vector<uint8> stream = { 0x03, 0x11, 0x22, 0x00, 0x01, 0x00, 0x01, 0x01, 0x01, 0x00, 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 };
	Feed feed = Run(stream, 16);
	Check(feed.packets.size() == 4, "three frames and an empty one give four packets");
	if (feed.packets.size() == 4) {
		Check(feed.packets[0] == std::vector<uint8>({ 0x11, 0x22 }), "first packet");
		Check(feed.packets[1].empty(), "empty packet from 01 00");
		Check(feed.packets[2] == std::vector<uint8>({ 0x00, 0x00 }), "packet of two zeros");
		Check(feed.packets[3] == std::vector<uint8>({ 0x11, 0x22, 0x33, 0x44 }), "fourth packet");
	}
	Check(feed.broken == 0 && feed.tooLong == 0, "no errors on clean frames");
}


static void TestGarbageAndResync() {
	// 05 claims four bytes, but a zero comes after two: the frame is broken. The next frame starts at that zero.
	std::vector<uint8> stream = { 0x05, 0x11, 0x22, 0x00, 0x03, 0xAA, 0xBB, 0x00 };
	Feed feed = Run(stream, 8);
	Check(feed.broken == 1, "a block cut by a zero is broken");
	Check(feed.packets.size() == 1 && feed.packets[0] == std::vector<uint8>({ 0xAA, 0xBB }), "the next frame decodes after the break");

	// Noise with no block open is nothing; a line that starts with zeros is fine.
	std::vector<uint8> noise = { 0x00, 0x00, 0x00, 0x02, 0x7E, 0x00, 0x00 };
	Feed noiseFeed = Run(noise, 8);
	Check(noiseFeed.packets.size() == 1 && noiseFeed.packets[0] == std::vector<uint8>({ 0x7E }), "zeros before a frame are ignored");
	Check(noiseFeed.broken == 0, "zeros alone are not a broken frame");

	// A frame that stops inside a block with no zero at all is only reported when the next zero arrives.
	std::vector<uint8> unfinished = { 0x04, 0x11, 0x22 };
	Feed unfinishedFeed = Run(unfinished, 8);
	Check(unfinishedFeed.packets.empty() && unfinishedFeed.broken == 0, "an open block is pending until a zero");
	unfinished.push_back(0x00);
	Feed closed = Run(unfinished, 8);
	Check(closed.broken == 1 && closed.packets.empty(), "the zero breaks the open block");
}


static void TestOverflow() {
	std::vector<uint8> stream = { 0x03, 0x11, 0x22, 0x00, 0x02, 0x33, 0x00 };
	Feed feed = Run(stream, 1);
	Check(feed.tooLong == 1, "a packet longer than out is tooLong once, at its delimiter");
	Check(feed.packets.size() == 1 && feed.packets[0] == std::vector<uint8>({ 0x33 }), "the frame after an overflow decodes");

	Feed exact = Run({ 0x03, 0x11, 0x22, 0x00 }, 2);
	Check(exact.packets.size() == 1 && exact.tooLong == 0, "out of exact size holds the packet");
}


static void TestReset() {
	Cobs::Decoder decoder;
	std::array<uint8, 8> out {};
	std::size_t length = 0;
	decoder.Push(0x05, std::span<uint8>(out), length);
	decoder.Push(0x11, std::span<uint8>(out), length);
	decoder.Reset();
	Cobs::Status status = decoder.Push(0x00, std::span<uint8>(out), length);
	Check(status == Cobs::Status::pending, "after Reset a zero is nothing");
	decoder.Push(0x02, std::span<uint8>(out), length);
	decoder.Push(0x44, std::span<uint8>(out), length);
	status = decoder.Push(0x00, std::span<uint8>(out), length);
	Check(status == Cobs::Status::packet && length == 1 && out[0] == 0x44, "Reset drops the partial frame");
}


// Constant-expression use: the decoder object and the sizes.
constexpr bool DecodesInConstantExpression() {
	constexpr std::array<uint8, 8> frame = { 0x03, 0x11, 0x22, 0x02, 0x33, 0x00, 0x00, 0x00 };
	std::array<uint8, 8> out {};
	Cobs::Decoder decoder;
	std::size_t length = 0;
	Cobs::Status status = Cobs::Status::pending;
	for (std::size_t i = 0; i < 6; i++) {
		status = decoder.Push(frame[i], std::span<uint8>(out), length);
	}

	return status == Cobs::Status::packet && length == 4 && out[0] == 0x11 && out[1] == 0x22 && out[2] == 0x00 && out[3] == 0x33;
}


static_assert(DecodesInConstantExpression(), "the decoder runs in a constant expression");
static_assert(Cobs::EncodedSize(0) == 2, "EncodedSize of an empty packet");
static_assert(Cobs::EncodedSize(254) == 257, "EncodedSize of 254 bytes");
static_assert(Cobs::EncodedSize(255) == 258, "EncodedSize of 255 bytes");


int main() {
	TestByteAtATime();
	TestGarbageAndResync();
	TestOverflow();
	TestReset();

	if (failures != 0) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}

	std::printf("TestCobsStream: all checks passed\n");
	return 0;
}
