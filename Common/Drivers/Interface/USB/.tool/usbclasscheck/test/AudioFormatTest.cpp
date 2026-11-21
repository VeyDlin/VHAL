#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "AudioFormat.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


bool Near(double value, double expected, double tolerance) {
	return std::fabs(value - expected) <= tolerance;
}


PcmFormat Format(std::uint32_t rate, std::uint16_t channels, std::uint16_t bits, bool isFloat) {
	PcmFormat format;
	format.sampleRate = rate;
	format.channels = channels;
	format.bitsPerSample = bits;
	format.isFloat = isFloat;
	return format;
}


void CheckSupport() {
	Expect(UnsupportedFormatReason(Format(48000, 2, 16, false)).empty(), "16-bit integer is supported");
	Expect(UnsupportedFormatReason(Format(48000, 2, 32, true)).empty(), "32-bit float is supported");
	Expect(UnsupportedFormatReason(Format(48000, 2, 8, false)).find("8 bits") != std::string::npos, "8-bit integer is refused");
	Expect(!UnsupportedFormatReason(Format(48000, 2, 24, true)).empty(), "24-bit float is refused");
	Expect(!UnsupportedFormatReason(Format(0, 2, 16, false)).empty(), "a zero sample rate is refused");
	Expect(!UnsupportedFormatReason(Format(48000, 0, 16, false)).empty(), "zero channels are refused");
}


void CheckFrameSizes() {
	Expect(BytesPerFrame(Format(48000, 2, 24, false)) == 6, "a stereo 24-bit frame is 6 bytes");
	Expect(BytesPerFrame(Format(48000, 1, 32, true)) == 4, "a mono float frame is 4 bytes");
}


void CheckDecode() {
	// Stereo 16-bit: 0x4000 is 16384, so 0.5 on the left and 0 on the right average to 0.25.
	PcmFormat stereo16 = Format(48000, 2, 16, false);
	std::vector<std::uint8_t> frames16 = {
		0x00, 0x40, 0x00, 0x00,
		0x00, 0x80, 0x00, 0x80,
		0xFF, 0x7F, 0xFF, 0x7F
	};
	std::vector<float> mono16 = DecodeMono(frames16.data(), 3, stereo16);
	Expect(mono16.size() == 3, "three stereo frames give three mono samples");
	Expect(Near(mono16[0], 0.25, 1e-6), "16-bit 0x4000 and 0 average to 0.25");
	Expect(Near(mono16[1], -1.0, 1e-6), "16-bit -32768 on both channels is -1.0");
	Expect(Near(mono16[2], 32767.0 / 32768.0, 1e-6), "16-bit 0x7FFF is 32767 / 32768");

	// Mono 24-bit: 0x7FFFFF is 8388607 and 0x800000 is -8388608.
	PcmFormat mono24 = Format(48000, 1, 24, false);
	std::vector<std::uint8_t> frames24 = {0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x80};
	std::vector<float> mono24Samples = DecodeMono(frames24.data(), 2, mono24);
	Expect(Near(mono24Samples[0], 8388607.0 / 8388608.0, 1e-7), "24-bit 0x7FFFFF is 8388607 / 8388608");
	Expect(Near(mono24Samples[1], -1.0, 1e-7), "24-bit 0x800000 is -1.0 after sign extension");

	// Mono 32-bit integer: 0x40000000 is 2^30, which is 0.5 of 2^31.
	PcmFormat mono32 = Format(48000, 1, 32, false);
	std::vector<std::uint8_t> frames32 = {0x00, 0x00, 0x00, 0x40};
	Expect(Near(DecodeMono(frames32.data(), 1, mono32)[0], 0.5, 1e-9), "32-bit 0x40000000 is 0.5");

	// Stereo float: 0.5 on the left and -0.5 on the right cancel; 0.75 alone is 0.75.
	PcmFormat stereoFloat = Format(48000, 2, 32, true);
	std::vector<std::uint8_t> floats = {
		0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0xBF,
		0x00, 0x00, 0x40, 0x3F, 0x00, 0x00, 0x40, 0x3F
	};
	std::vector<float> monoFloat = DecodeMono(floats.data(), 2, stereoFloat);
	Expect(Near(monoFloat[0], 0.0, 1e-9), "float 0.5 and -0.5 average to zero");
	Expect(Near(monoFloat[1], 0.75, 1e-9), "float 0.75 on both channels is 0.75");
}


void CheckEncode() {
	// 0.5 in 16 bits is 16384, which is 0x4000, written little endian on both channels.
	PcmFormat stereo16 = Format(48000, 2, 16, false);
	std::vector<std::uint8_t> half = EncodeMono(std::vector<float>{0.5f}, stereo16);
	std::vector<std::uint8_t> halfExpected = {0x00, 0x40, 0x00, 0x40};
	Expect(half == halfExpected, "0.5 in stereo 16-bit is 0x4000 on both channels");

	std::vector<std::uint8_t> peak = EncodeMono(std::vector<float>{1.0f, 2.0f, -1.0f, -2.0f}, Format(48000, 1, 16, false));
	std::vector<std::uint8_t> peakExpected = {0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x80, 0x00, 0x80};
	Expect(peak == peakExpected, "1.0 and 2.0 clamp to 0x7FFF, and -1.0 and -2.0 to 0x8000");

	// 0.5 in 24 bits is 4194304, which is 0x400000; -0.5 is 0xC00000 in two's complement.
	std::vector<std::uint8_t> bits24 = EncodeMono(std::vector<float>{0.5f, -0.5f, 1.0f}, Format(48000, 1, 24, false));
	std::vector<std::uint8_t> bits24Expected = {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0, 0xFF, 0xFF, 0x7F};
	Expect(bits24 == bits24Expected, "24-bit encoding of 0.5, -0.5 and 1.0");

	// 0.5 in 32 bits is 2^30, which is 0x40000000.
	std::vector<std::uint8_t> bits32 = EncodeMono(std::vector<float>{0.5f}, Format(48000, 1, 32, false));
	std::vector<std::uint8_t> bits32Expected = {0x00, 0x00, 0x00, 0x40};
	Expect(bits32 == bits32Expected, "32-bit integer encoding of 0.5");

	// 0.75 as a float is 0x3F400000.
	std::vector<std::uint8_t> flt = EncodeMono(std::vector<float>{0.75f}, Format(48000, 1, 32, true));
	std::vector<std::uint8_t> fltExpected = {0x00, 0x00, 0x40, 0x3F};
	Expect(flt == fltExpected, "float encoding of 0.75");
}


void CheckRoundTrip() {
	std::vector<float> values = {0.0f, 0.123f, -0.456f, 0.999f, -0.999f, 0.25f};
	PcmFormat formats[] = {
		Format(48000, 2, 16, false),
		Format(48000, 1, 24, false),
		Format(48000, 2, 32, false),
		Format(48000, 2, 32, true)
	};
	for (const PcmFormat& format : formats) {
		std::vector<std::uint8_t> bytes = EncodeMono(values, format);
		std::vector<float> back = DecodeMono(bytes.data(), values.size(), format);
		bool close = back.size() == values.size();
		// The 16-bit step is 1/32768, so the round trip is within one step.
		for (std::size_t index = 0; close && index < values.size(); index++) {
			if (std::fabs(back[index] - values[index]) > 1.0 / 32768.0) {
				close = false;
			}
		}

		Expect(close, "a value written and read back is within one 16-bit step");
	}
}


void CheckText() {
	Expect(FormatText(Format(48000, 1, 32, true)) == "48000 Hz, 1 channel, 32-bit float", "a mono float format is described");
	Expect(FormatText(Format(48000, 2, 16, false)) == "48000 Hz, 2 channels, 16-bit integer", "a stereo 16-bit format is described");
}


}


int main() {
	CheckSupport();
	CheckFrameSizes();
	CheckDecode();
	CheckEncode();
	CheckRoundTrip();
	CheckText();

	std::printf("usbclasscheck audio format test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
