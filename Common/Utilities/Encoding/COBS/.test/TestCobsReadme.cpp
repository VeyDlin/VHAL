#include <Utilities/DataTypes.h>
#include <Utilities/Encoding/COBS/Cobs.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>


// The examples of the COBS wiki page (Encoding/COBS), run as the page writes them. The values the examples give are checked.

using Encoding::Cobs;

static_assert(Cobs::EncodedSize(254) == 257);


namespace {


int failures = 0;


void Check(bool condition, const char* what) {
	if (!condition) {
		failures++;
		std::printf("TestCobsReadme.cpp: %s\n", what);
	}
}


void QuickStart() {
	std::array<uint8, 4> packet = { 0x11, 0x22, 0x00, 0x33 };
	std::array<uint8, Cobs::EncodedSize(4)> frame {};

	// Encode: the frame is frame[0 .. length), its last byte is the zero delimiter
	Result<std::size_t> encoded = Cobs::Encode(std::span<const uint8>(packet), std::span<uint8>(frame));
	if (encoded.IsOk()) {
		// Send frame[0 .. encoded.Value())
	}

	Check(encoded.IsOk() && encoded.Value() == 6, "Encode of 11 22 00 33 gives six bytes");
	const std::array<uint8, 6> expected = { 0x03, 0x11, 0x22, 0x02, 0x33, 0x00 };
	Check(frame == expected, "the frame of 11 22 00 33 is 03 11 22 02 33 00");

	// Decode in place: the packet is written over the frame
	Result<std::size_t> decoded = Cobs::Decode(std::span<uint8>(frame).first(encoded.Value()));
	Check(decoded.IsOk() && decoded.Value() == 4, "in-place Decode gives four bytes");
	Check(frame[0] == 0x11 && frame[1] == 0x22 && frame[2] == 0x00 && frame[3] == 0x33, "in-place Decode gives the packet back");
}


void EncodeUnchecked() {
	std::array<uint8, 4> packet = { 0x11, 0x22, 0x00, 0x33 };
	std::array<uint8, Cobs::EncodedSize(4)> frame {};

	// The caller has checked that frame holds EncodedSize(packet.size()) bytes
	std::size_t length = Cobs::EncodeUnchecked(std::span<const uint8>(packet), std::span<uint8>(frame));
	Check(length == 6 && frame[5] == 0x00, "EncodeUnchecked writes the six-byte frame");
}


void DecodeToOtherSpan() {
	const std::array<uint8, 6> frame = { 0x03, 0x11, 0x22, 0x02, 0x33, 0x00 };
	std::array<uint8, 4> packet {};

	// The packet is written into packet; frame is read only
	Result<std::size_t> decoded = Cobs::Decode(std::span<const uint8>(frame), std::span<uint8>(packet));
	Check(decoded.IsOk() && decoded.Value() == 4, "Decode into another span gives four bytes");
	Check(packet[0] == 0x11 && packet[1] == 0x22 && packet[2] == 0x00 && packet[3] == 0x33, "Decode into another span gives the packet");
}


void DecoderExample() {
	// The bytes that arrive on the line: one frame, then the next frame's bytes
	const std::array<uint8, 8> received = { 0x03, 0x11, 0x22, 0x02, 0x33, 0x00, 0x01, 0x00 };

	Cobs::Decoder decoder;
	std::array<uint8, 32> packet {};
	std::size_t length = 0;

	for (uint8 byte : received) {
		if (decoder.Push(byte, std::span<uint8>(packet), length) == Cobs::Status::packet) {
			// packet[0 .. length) is a complete packet
		}
	}

	// The loop above ends with the empty second frame, which sets length to zero. The first frame alone is fed to a second decoder
	// to check the packet it completes.
	Cobs::Decoder single;
	std::size_t singleLength = 0;
	Cobs::Status status = Cobs::Status::pending;
	for (std::size_t index = 0; index < 6; index++) {
		status = single.Push(received[index], std::span<uint8>(packet), singleLength);
	}

	Check(status == Cobs::Status::packet && singleLength == 4, "the decoder completes the first frame at its delimiter");
	Check(packet[0] == 0x11 && packet[1] == 0x22 && packet[2] == 0x00 && packet[3] == 0x33, "the decoder writes the first packet");
	Check(length == 0, "the empty second frame leaves length at zero");
}


void DecoderReset() {
	Cobs::Decoder decoder;
	std::array<uint8, 8> packet {};
	std::size_t length = 0;

	decoder.Push(0x05, std::span<uint8>(packet), length); // a block of four bytes is open
	decoder.Reset();                                      // the partial frame is dropped

	// The frame that follows decodes from its own bytes
	Cobs::Status status = Cobs::Status::pending;
	for (uint8 byte : std::array<uint8, 3> { 0x02, 0x44, 0x00 }) {
		status = decoder.Push(byte, std::span<uint8>(packet), length);
	}

	Check(status == Cobs::Status::packet && length == 1 && packet[0] == 0x44, "Reset drops the partial frame");
}
}


int main() {
	QuickStart();
	EncodeUnchecked();
	DecodeToOtherSpan();
	DecoderExample();
	DecoderReset();

	if (failures != 0) {
		std::printf("%d COBS readme check(s) failed\n", failures);
		return 1;
	}

	std::printf("COBS readme examples passed\n");
	return 0;
}
