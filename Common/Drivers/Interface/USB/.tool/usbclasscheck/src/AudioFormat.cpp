#include "AudioFormat.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Text.h"

namespace {

// The full scale of each integer layout: a sample of -2^(bits-1) is -1.0, and the scale is 2^(bits-1), so the positive peak is one step
// short of 1.0 and nothing overflows.
constexpr double Int16Scale = 32768.0;
constexpr double Int24Scale = 8388608.0;
constexpr double Int32Scale = 2147483648.0;


std::uint32_t ReadLittleEndian(const std::uint8_t* data, std::size_t bytes) {
	std::uint32_t value = 0;
	for (std::size_t index = 0; index < bytes; index++) {
		value |= static_cast<std::uint32_t>(data[index]) << (8 * index);
	}

	return value;
}


void WriteLittleEndian(std::uint8_t* data, std::uint32_t value, std::size_t bytes) {
	for (std::size_t index = 0; index < bytes; index++) {
		data[index] = static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF);
	}
}


// One sample of one channel, at the start of its bytes, as a value in -1 to 1.
float DecodeSample(const std::uint8_t* data, const PcmFormat& format) {
	if (format.isFloat) {
		return std::bit_cast<float>(ReadLittleEndian(data, 4));
	}

	if (format.bitsPerSample == 16) {
		std::int16_t value = static_cast<std::int16_t>(ReadLittleEndian(data, 2));
		return static_cast<float>(static_cast<double>(value) / Int16Scale);
	}

	if (format.bitsPerSample == 24) {
		std::uint32_t raw = ReadLittleEndian(data, 3);
		// Sign-extend the 24-bit value to 32 bits.
		if ((raw & 0x800000) != 0) {
			raw |= 0xFF000000;
		}

		std::int32_t value = static_cast<std::int32_t>(raw);
		return static_cast<float>(static_cast<double>(value) / Int24Scale);
	}

	std::int32_t value = static_cast<std::int32_t>(ReadLittleEndian(data, 4));
	return static_cast<float>(static_cast<double>(value) / Int32Scale);
}


// Writes one sample at the start of its bytes. The value is clamped to -1 to 1 first.
void EncodeSample(std::uint8_t* data, float sample, const PcmFormat& format) {
	double clamped = static_cast<double>(sample);
	if (clamped > 1.0) {
		clamped = 1.0;
	}

	if (clamped < -1.0) {
		clamped = -1.0;
	}

	if (format.isFloat) {
		WriteLittleEndian(data, std::bit_cast<std::uint32_t>(static_cast<float>(clamped)), 4);
		return;
	}

	// The positive peak is one step short of the scale, so the rounded value stays inside the layout.
	if (format.bitsPerSample == 16) {
		long rounded = std::lround(clamped * Int16Scale);
		if (rounded > 32767) {
			rounded = 32767;
		}

		WriteLittleEndian(data, static_cast<std::uint32_t>(static_cast<std::int32_t>(rounded)), 2);
		return;
	}

	if (format.bitsPerSample == 24) {
		long rounded = std::lround(clamped * Int24Scale);
		if (rounded > 8388607) {
			rounded = 8388607;
		}

		WriteLittleEndian(data, static_cast<std::uint32_t>(static_cast<std::int32_t>(rounded)), 3);
		return;
	}

	long long rounded = std::llround(clamped * Int32Scale);
	if (rounded > 2147483647LL) {
		rounded = 2147483647LL;
	}

	WriteLittleEndian(data, static_cast<std::uint32_t>(static_cast<std::int32_t>(rounded)), 4);
}


std::size_t SampleBytes(const PcmFormat& format) {
	return format.bitsPerSample / 8;
}

}


std::string UnsupportedFormatReason(const PcmFormat& format) {
	if (format.sampleRate == 0) {
		return "the sample rate is zero";
	}

	if (format.channels == 0) {
		return "the channel count is zero";
	}

	if (format.isFloat) {
		if (format.bitsPerSample != 32) {
			return "float samples of " + std::to_string(format.bitsPerSample) + " bits are not supported";
		}

		return "";
	}

	if (format.bitsPerSample != 16 && format.bitsPerSample != 24 && format.bitsPerSample != 32) {
		return "integer samples of " + std::to_string(format.bitsPerSample) + " bits are not supported";
	}

	return "";
}


std::size_t BytesPerFrame(const PcmFormat& format) {
	return static_cast<std::size_t>(format.channels) * SampleBytes(format);
}


std::vector<float> DecodeMono(const std::uint8_t* data, std::size_t frames, const PcmFormat& format) {
	std::vector<float> mono(frames, 0.0f);
	std::size_t sampleBytes = SampleBytes(format);
	std::size_t frameBytes = BytesPerFrame(format);
	for (std::size_t frame = 0; frame < frames; frame++) {
		double sum = 0.0;
		for (std::size_t channel = 0; channel < format.channels; channel++) {
			sum += static_cast<double>(DecodeSample(data + frame * frameBytes + channel * sampleBytes, format));
		}

		mono[frame] = static_cast<float>(sum / static_cast<double>(format.channels));
	}

	return mono;
}


std::vector<std::uint8_t> EncodeMono(const std::vector<float>& mono, const PcmFormat& format) {
	std::size_t sampleBytes = SampleBytes(format);
	std::size_t frameBytes = BytesPerFrame(format);
	std::vector<std::uint8_t> bytes(mono.size() * frameBytes, 0);
	for (std::size_t frame = 0; frame < mono.size(); frame++) {
		for (std::size_t channel = 0; channel < format.channels; channel++) {
			EncodeSample(bytes.data() + frame * frameBytes + channel * sampleBytes, mono[frame], format);
		}
	}

	return bytes;
}


std::string FormatText(const PcmFormat& format) {
	std::string text;
	AppendFormat(text, "%u Hz, %u %s, ", static_cast<unsigned int>(format.sampleRate), static_cast<unsigned int>(format.channels),
		format.channels == 1 ? "channel" : "channels");
	if (format.isFloat) {
		text += "32-bit float";
	} else {
		AppendFormat(text, "%u-bit integer", static_cast<unsigned int>(format.bitsPerSample));
	}

	return text;
}
