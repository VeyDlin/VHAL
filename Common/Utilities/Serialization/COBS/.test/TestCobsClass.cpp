#include <VHAL.h>
#include <Utilities/Serialization/COBS/COBS.h>
#include <VhalCheck.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>


// Host tests of the COBS class template (Serialization/COBS/COBS.h): payloads of every length near the 254 and 255 block limits,
// escaped bytes, the round trip over 0 to 600 bytes, a custom configuration, the encoder's output bound, and the decoder's rejection
// of a zero code, a truncated block and a frame without its start byte.


namespace {


// Deterministic pseudo-random bytes so every run covers the same payloads.
std::vector<uint8> MakePayload(size_t length, uint32 seed, bool allowSpecial) {
	std::vector<uint8> payload(length);
	uint32 state = seed * 2654435761u + 12345u;
	static const uint8 specials[] = { 0x00, 0x01, 0x02, 0x55, 0xAA, 0xBB, 0xFE, 0xFF };
	for (size_t i = 0; i < length; ++i) {
		state = state * 1103515245u + 12345u;
		uint8 candidate = static_cast<uint8>(state >> 16);
		if (allowSpecial) {
			payload[i] = (candidate % 3 == 0) ? specials[(state >> 8) % (sizeof(specials) / sizeof(specials[0]))] : candidate;
		} else {
			payload[i] = (candidate == 0x00 || candidate == 0xFE || candidate == 0xFF) ? 0x11 : candidate;
		}
	}
	return payload;
}


// Encodes and decodes the input. The result is false on an error or a mismatch, and details says which; the details are also
// printed to stderr, so a failing check shows the length.
template <size_t TempBufferSize>
bool RoundTrip(const COBS<TempBufferSize>& cobs, const std::vector<uint8>& input, std::string& details) {
	std::vector<uint8> encoded(input.size() * 2 + 16);
	Result<size_t> encodeResult = cobs.Encode(input.data(), input.size(), encoded.data(), encoded.size());
	if (encodeResult.IsErr()) {
		details = "Encode failed for length " + std::to_string(input.size());
		std::fprintf(stderr, "%s\n", details.c_str());
		return false;
	}

	std::vector<uint8> decoded(input.size() + 16);
	Result<size_t> decodeResult = cobs.Decode(encoded.data(), encodeResult.Value(), decoded.data(), decoded.size());
	if (decodeResult.IsErr()) {
		details = "Decode failed for length " + std::to_string(input.size());
		std::fprintf(stderr, "%s\n", details.c_str());
		return false;
	}

	bool sameBytes = input.empty() || std::memcmp(decoded.data(), input.data(), input.size()) == 0;
	if (decodeResult.Value() != input.size() || !sameBytes) {
		details = "round trip mismatch for length " + std::to_string(input.size());
		std::fprintf(stderr, "%s\n", details.c_str());
		return false;
	}

	return true;
}


void CheckPlainBytes0to254() {
	COBS<1024> cobs({});
	std::string details;
	for (size_t length = 0; length <= 254; ++length) {
		std::vector<uint8> payload = MakePayload(length, static_cast<uint32>(length), false);
		bool ok = RoundTrip(cobs, payload, details);
		VHAL_CHECK(ok);
		if (!ok) {
			return;
		}
	}
}


void CheckEscapedBytesShort() {
	COBS<1024> cobs({});
	std::string details;
	for (size_t length = 0; length <= 100; ++length) {
		std::vector<uint8> payload = MakePayload(length, static_cast<uint32>(length + 7), true);
		bool ok = RoundTrip(cobs, payload, details);
		VHAL_CHECK(ok);
		if (!ok) {
			return;
		}
	}
}


void CheckRun254() {
	COBS<1024> cobs({});
	std::string details;
	std::vector<uint8> payload(254, 0x11);
	VHAL_CHECK(RoundTrip(cobs, payload, details));
}


void CheckRun255() {
	COBS<1024> cobs({});
	std::string details;
	std::vector<uint8> payload(255, 0x11);
	VHAL_CHECK(RoundTrip(cobs, payload, details));
}


void CheckRoundTrip0to600() {
	COBS<2048> cobs({});
	std::vector<size_t> failingLengths;
	for (size_t length = 0; length <= 600; ++length) {
		std::string details;
		std::vector<uint8> payload = MakePayload(length, static_cast<uint32>(length * 31 + 3), true);
		if (!RoundTrip(cobs, payload, details)) {
			failingLengths.push_back(length);
		}
	}
	if (!failingLengths.empty()) {
		std::fprintf(stderr, "%zu lengths fail, first %zu\n", failingLengths.size(), failingLengths.front());
	}
	VHAL_CHECK(failingLengths.empty());
}


void CheckCustomConfigShort() {
	COBS<256> cobs({ 0xAA, 0x55, 0xBB, 0x01, 0x02, 0x03 });
	std::string details;
	for (size_t length = 0; length <= 100; ++length) {
		std::vector<uint8> payload = MakePayload(length, static_cast<uint32>(length + 99), true);
		bool ok = RoundTrip(cobs, payload, details);
		VHAL_CHECK(ok);
		if (!ok) {
			return;
		}
	}
}


void CheckEncodeEmpty() {
	COBS<256> cobs({});
	uint8 output[16] = {};
	Result<size_t> result = cobs.Encode(nullptr, 0, output, sizeof(output));
	VHAL_CHECK(result.IsOk());
	if (result.IsOk()) {
		VHAL_CHECK_EQ(result.Value(), 3);
		VHAL_CHECK(output[0] == 0xFF && output[1] == 0x01 && output[2] == 0x00);
	}
}


void CheckEncodeExactSizeAccepted() {
	COBS<256> cobs({});
	std::vector<uint8> payload = { 1, 2, 3 };
	std::vector<uint8> big(64);
	Result<size_t> full = cobs.Encode(payload.data(), payload.size(), big.data(), big.size());
	VHAL_CHECK(full.IsOk());
	if (full.IsOk()) {
		std::unique_ptr<uint8[]> exact(new uint8[full.Value()]);
		Result<size_t> result = cobs.Encode(payload.data(), payload.size(), exact.get(), full.Value());
		VHAL_CHECK(result.IsOk() && result.Value() == full.Value());
	}
}


void CheckEncodeOutputBoundOffByOne() {
	COBS<256> cobs({});
	std::vector<uint8> payload = { 1, 2, 3 };
	std::vector<uint8> big(64);
	Result<size_t> full = cobs.Encode(payload.data(), payload.size(), big.data(), big.size());
	VHAL_CHECK(full.IsOk());
	if (full.IsOk()) {
		std::unique_ptr<uint8[]> tooSmall(new uint8[full.Value() - 1]);
		Result<size_t> result = cobs.Encode(payload.data(), payload.size(), tooSmall.get(), full.Value() - 1);
		VHAL_CHECK(result.IsErr() && result.Error() == ResultStatus::bufferOverflow);
	}
}


void CheckDecodeZeroCodeRejected() {
	COBS<256> cobs({});
	uint8 frame[] = { 0xFF, 0x00, 0x00 };
	uint8 output[16] = {};
	Result<size_t> result = cobs.Decode(frame, sizeof(frame), output, sizeof(output));
	VHAL_CHECK(result.IsErr() && result.Error() == ResultStatus::dataCorrupted);
}


void CheckDecodeTruncatedRejected() {
	COBS<256> cobs({});
	uint8 frame[] = { 0xFF, 0x05, 0x01, 0x02, 0x00 };
	uint8 output[16] = {};
	Result<size_t> result = cobs.Decode(frame, sizeof(frame), output, sizeof(output));
	VHAL_CHECK(result.IsErr() && result.Error() == ResultStatus::dataCorrupted);
}


void CheckDecodeMissingStartRejected() {
	COBS<256> cobs({});
	uint8 frame[] = { 0xAA, 0x01, 0x00 };
	uint8 output[16] = {};
	Result<size_t> result = cobs.Decode(frame, sizeof(frame), output, sizeof(output));
	VHAL_CHECK(result.IsErr() && result.Error() == ResultStatus::invalidArgument);
}


}


int main() {
	CheckPlainBytes0to254();
	CheckEscapedBytesShort();
	CheckRun254();
	CheckRun255();
	CheckRoundTrip0to600();
	CheckCustomConfigShort();
	CheckEncodeEmpty();
	CheckEncodeExactSizeAccepted();
	CheckEncodeOutputBoundOffByOne();
	CheckDecodeZeroCodeRejected();
	CheckDecodeTruncatedRejected();
	CheckDecodeMissingStartRejected();
	return VhalCheck::Finish("TestCobsClass");
}
