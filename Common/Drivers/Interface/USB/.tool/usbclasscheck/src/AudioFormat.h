#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The sample layout of a shared-mode audio stream, as the Windows mix format describes it. Only the layouts that USB audio
// endpoints report are decoded: 16, 24 and 32-bit integers, and 32-bit float. Samples are little endian.
struct PcmFormat {
	std::uint32_t sampleRate = 0;
	std::uint16_t channels = 0;
	// The container size of one sample, which is the bit count for these layouts (24 means three packed bytes).
	std::uint16_t bitsPerSample = 0;
	bool isFloat = false;
};

// Empty when the format can be decoded, otherwise the reason it cannot.
std::string UnsupportedFormatReason(const PcmFormat& format);

// The bytes of one frame (one sample of every channel).
std::size_t BytesPerFrame(const PcmFormat& format);

// Converts interleaved frames to mono samples in -1 to 1 by averaging the channels. data holds frames * BytesPerFrame bytes.
std::vector<float> DecodeMono(const std::uint8_t* data, std::size_t frames, const PcmFormat& format);

// Writes mono samples as interleaved frames with the same value on every channel. Values outside -1 to 1 are clamped.
std::vector<std::uint8_t> EncodeMono(const std::vector<float>& mono, const PcmFormat& format);

// The format for a report, such as "48000 Hz, 1 channel, 32-bit float".
std::string FormatText(const PcmFormat& format);
