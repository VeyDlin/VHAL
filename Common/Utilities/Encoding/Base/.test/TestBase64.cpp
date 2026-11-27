#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Encoding/Base/Base64.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


// Host tests of Base64: round trips over every length up to 1100 bytes, the known vectors, long input that a fixed intermediate buffer
// would truncate, the whitespace and the invalid inputs of the decoder (refused with no output written), and the output size rules of
// the encoder.


namespace {


std::vector<uint8> MakePattern(size_t size, uint32 seed) {
	std::vector<uint8> data(size);
	uint32 state = seed;

	for (size_t i = 0; i < size; ++i) {
		state = state * 1664525u + 1013904223u;
		data[i] = static_cast<uint8>(state >> 24);
	}

	return data;
}


std::string EncodeToString(std::span<const uint8> data) {
	std::vector<char> encoded(Base64::GetEncodedSize(data.size()));
	size_t written = Base64::Encode(std::span(encoded), data);
	return std::string(encoded.data(), written);
}


} // namespace


void RoundTripLengths0To1100() {
	for (size_t size = 0; size <= 1100; ++size) {
		std::vector<uint8> original = MakePattern(size, static_cast<uint32>(size + 1));
		std::string encoded = EncodeToString(std::span(original));

		std::vector<uint8> decoded(size + 3);
		size_t decodedSize = 0;
		bool ok = Base64::Decode(encoded, std::span(decoded), decodedSize);

		VHAL_CHECK(ok);
		VHAL_CHECK_EQ(decodedSize, size);
		VHAL_CHECK(std::equal(original.begin(), original.end(), decoded.begin()));
		if (VhalCheck::failCount > 0) {
			return;
		}
	}
}


void KnownVectors() {
	const std::vector<std::pair<std::string_view, std::string_view>> vectors = {
		{"", ""},
		{"f", "Zg=="},
		{"fo", "Zm8="},
		{"foo", "Zm9v"},
		{"foob", "Zm9vYg=="},
		{"fooba", "Zm9vYmE="},
		{"foobar", "Zm9vYmFy"},
	};

	for (const std::pair<std::string_view, std::string_view>& entry : vectors) {
		std::string_view plain = entry.first;
		std::string_view encoded = entry.second;

		std::vector<char> out(Base64::GetEncodedSize(plain.size()) + 1);
		size_t written = Base64::Encode(std::span(out.data(), out.size() - 1), plain);
		VHAL_CHECK_EQ(std::string_view(out.data(), written), encoded);

		std::vector<uint8> decoded(plain.size() + 1);
		size_t decodedSize = 99;
		VHAL_CHECK(Base64::Decode(encoded, std::span(decoded), decodedSize));
		VHAL_CHECK_EQ(std::string_view(reinterpret_cast<const char*>(decoded.data()), decodedSize), plain);
	}
}


void DecodeLongInputNotTruncated() {
	// 750 bytes encode to 1000 characters, well past the old 512-character internal buffer.
	std::vector<uint8> original = MakePattern(750, 7);
	std::string encoded = EncodeToString(std::span(original));
	VHAL_CHECK_EQ(encoded.size(), 1000);

	std::vector<uint8> decoded(750);
	size_t decodedSize = 0;
	VHAL_CHECK(Base64::Decode(encoded, std::span(decoded), decodedSize));
	VHAL_CHECK_EQ(decodedSize, 750);
	VHAL_CHECK(decoded == original);
}


void DecodeWhitespaceSkipped() {
	std::array<uint8, 16> decoded{};
	size_t decodedSize = 0;

	VHAL_CHECK(Base64::Decode("Zm9v\r\nYmFy\t \n", std::span(decoded), decodedSize));
	VHAL_CHECK_EQ(std::string_view(reinterpret_cast<const char*>(decoded.data()), decodedSize), "foobar");

	VHAL_CHECK(Base64::Decode(" Zm9v YmFy ", std::span(decoded), decodedSize));
	VHAL_CHECK_EQ(std::string_view(reinterpret_cast<const char*>(decoded.data()), decodedSize), "foobar");
}


void DecodeEmpty() {
	std::array<uint8, 4> decoded{};
	size_t decodedSize = 99;

	VHAL_CHECK(Base64::Decode("", std::span(decoded), decodedSize));
	VHAL_CHECK_EQ(decodedSize, 0);

	std::span<uint8> noOutput;
	VHAL_CHECK(Base64::Decode("", noOutput, decodedSize));
	VHAL_CHECK_EQ(decodedSize, 0);
}


void DecodeInvalidCharacters() {
	const std::vector<std::string_view> invalidInputs = {
		"Zm9v!YmFy",
		"Zm9v-_AA",
		"Zm9v\xff" "AAAA",
		"Zm9v\x80" "AAAA",
		std::string_view("Zm9v\0AAAA", 9),
	};

	for (std::string_view input : invalidInputs) {
		std::array<uint8, 16> decoded{};
		size_t decodedSize = 99;
		VHAL_CHECK(!Base64::Decode(input, std::span(decoded), decodedSize));
		VHAL_CHECK_EQ(decodedSize, 0);
	}
}


void DecodeBadLength() {
	const std::vector<std::string_view> invalidInputs = {"Zm9", "Zg=", "Zm9vYg=", "Zm9vYmFyA"};

	for (std::string_view input : invalidInputs) {
		std::array<uint8, 16> decoded{};
		size_t decodedSize = 99;
		VHAL_CHECK(!Base64::Decode(input, std::span(decoded), decodedSize));
		VHAL_CHECK_EQ(decodedSize, 0);
	}
}


void DecodeBadPadding() {
	const std::vector<std::string_view> invalidInputs = {
		"Z===",
		"A===",
		"=Zg=",
		"Zg==Zg==",
		"Zm=v",
		"Zg=A",
		"Zm9v=YmE=",
	};

	for (std::string_view input : invalidInputs) {
		std::array<uint8, 16> decoded{};
		size_t decodedSize = 99;
		VHAL_CHECK(!Base64::Decode(input, std::span(decoded), decodedSize));
		VHAL_CHECK_EQ(decodedSize, 0);
	}
}


void DecodeOutputTooSmall() {
	std::array<uint8, 5> tooSmall{};
	size_t decodedSize = 99;

	VHAL_CHECK(!Base64::Decode("Zm9vYmFy", std::span(tooSmall), decodedSize));
	VHAL_CHECK_EQ(decodedSize, 0);

	std::array<uint8, 6> exact{};
	VHAL_CHECK(Base64::Decode("Zm9vYmFy", std::span(exact), decodedSize));
	VHAL_CHECK_EQ(decodedSize, 6);
}


void DecodeInvalidWritesNothing() {
	std::array<uint8, 8> decoded{};
	decoded.fill(0xAA);
	size_t decodedSize = 99;

	VHAL_CHECK(!Base64::Decode("Zm9v!YmFy", std::span(decoded), decodedSize));
	VHAL_CHECK_EQ(decodedSize, 0);
	for (uint8 byte : decoded) {
		VHAL_CHECK_EQ(byte, 0xAA);
		if (VhalCheck::failCount > 0) {
			break;
		}
	}
}


void DecodeToBufferLargeAndTooSmall() {
	// 1500 bytes encode to 2000 characters: larger than the old 1024-byte intermediate buffer.
	std::vector<uint8> original = MakePattern(1500, 11);
	std::string encoded = EncodeToString(std::span(original));

	std::vector<char> text(1500);
	size_t textSize = 0;
	VHAL_CHECK(Base64::DecodeToBuffer(encoded, std::span(text), textSize));
	VHAL_CHECK_EQ(textSize, 1500);
	VHAL_CHECK(std::equal(original.begin(), original.end(), reinterpret_cast<const uint8*>(text.data())));

	std::vector<char> smaller(1499);
	VHAL_CHECK(!Base64::DecodeToBuffer(encoded, std::span(smaller), textSize));
	VHAL_CHECK_EQ(textSize, 0);
}


void DecodeToArrayTemplate() {
	std::array<uint8, 16> decoded{};
	size_t decodedSize = 0;

	VHAL_CHECK(Base64::DecodeToArray("SGVsbG8gV29ybGQ=", decoded, decodedSize));
	VHAL_CHECK_EQ(std::string_view(reinterpret_cast<const char*>(decoded.data()), decodedSize), "Hello World");
}


void EncodeStringOverload() {
	std::array<char, 32> out{};

	size_t written = Base64::Encode(std::span(out), "Hello World");

	VHAL_CHECK_EQ(std::string_view(out.data(), written), "SGVsbG8gV29ybGQ=");
}


void EncodeTooSmallReturnsZero() {
	std::array<char, 7> tooSmall{};
	std::array<uint8, 4> data = {'f', 'o', 'o', 'b'};

	VHAL_CHECK_EQ(Base64::Encode(std::span(tooSmall), std::span(data)), 0);
}


void EncodeWritesNoNul() {
	std::array<char, 9> out{};
	out.fill('#');
	std::array<uint8, 4> data = {'f', 'o', 'o', 'b'};

	size_t written = Base64::Encode(std::span(out), std::span(data));

	VHAL_CHECK_EQ(written, 8);
	VHAL_CHECK_EQ(std::string_view(out.data(), written), "Zm9vYg==");
	VHAL_CHECK(out[8] == '#');
}


void EncodeEmptyAndSizes() {
	std::array<char, 4> out{};
	std::span<const uint8> empty;

	VHAL_CHECK_EQ(Base64::Encode(std::span(out), empty), 0);
	VHAL_CHECK_EQ(Base64::GetEncodedSize(10), 16);
	VHAL_CHECK_EQ(Base64::GetDecodedSize(16), 12);
}


int main() {
	RoundTripLengths0To1100();
	KnownVectors();
	DecodeLongInputNotTruncated();
	DecodeWhitespaceSkipped();
	DecodeEmpty();
	DecodeInvalidCharacters();
	DecodeBadLength();
	DecodeBadPadding();
	DecodeOutputTooSmall();
	DecodeInvalidWritesNothing();
	DecodeToBufferLargeAndTooSmall();
	DecodeToArrayTemplate();
	EncodeStringOverload();
	EncodeTooSmallReturnsZero();
	EncodeWritesNoNul();
	EncodeEmptyAndSizes();

	return VhalCheck::Finish("TestBase64");
}
