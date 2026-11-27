// SHA-1 vectors: FIPS 180 examples, incremental updates across the padding boundaries, and the RFC 6455 accept key.
#include <VhalCheck.h>
#include <Utilities/Crypto/SHA/SHA1.h>
#include <Utilities/Encoding/Base/Base64.h>
#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>


static std::string HexDigest(const std::array<uint8, 20>& digest) {
	std::string text;
	for (uint8 byte : digest) {
		char pair[3];
		std::snprintf(pair, sizeof(pair), "%02x", byte);
		text += pair;
	}

	return text;
}


static void FipsAbc() {
	SHA1Hasher hasher;
	hasher.Update(std::string_view("abc"));
	VHAL_CHECK_EQ(HexDigest(hasher.Finalize()), "a9993e364706816aba3e25717850c26c9cd0d89d");
}


static void FipsEmpty() {
	SHA1Hasher hasher;
	VHAL_CHECK_EQ(HexDigest(hasher.Finalize()), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}


static void Fips448Bit() {
	SHA1Hasher hasher;
	hasher.Update(std::string_view("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
	VHAL_CHECK_EQ(HexDigest(hasher.Finalize()), "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}


static void FipsMillionA() {
	SHA1Hasher hasher;
	std::string chunk(1000, 'a');
	for (int i = 0; i < 1000; i++) {
		hasher.Update(std::string_view(chunk));
	}

	VHAL_CHECK_EQ(HexDigest(hasher.Finalize()), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}


// Splitting the input must not change the digest, including at the 55/56/63/64-byte padding edges.
static void IncrementalMatchesOneShot() {
	for (size_t length = 0; length <= 130; length++) {
		std::string message(length, 'x');
		for (size_t i = 0; i < length; i++) {
			message[i] = static_cast<char>('a' + (i % 26));
		}

		SHA1Hasher oneShot;
		oneShot.Update(std::string_view(message));
		std::string expected = HexDigest(oneShot.Finalize());

		SHA1Hasher split;
		size_t first = length / 3;
		split.Update(std::string_view(message.data(), first));
		split.Update(std::string_view(message.data() + first, length - first));
		VHAL_CHECK_EQ(HexDigest(split.Finalize()), expected);
	}
}


static void ResetReuses() {
	SHA1Hasher hasher;
	hasher.Update(std::string_view("abc"));
	hasher.Finalize();
	hasher.Reset();
	VHAL_CHECK_EQ(HexDigest(hasher.Finalize()), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}


// RFC 6455 section 1.3: the accept key is base64(SHA-1(key + GUID)).
static void Rfc6455AcceptExample() {
	SHA1Hasher hasher;
	hasher.Update(std::string_view("dGhlIHNhbXBsZSBub25jZQ=="));
	hasher.Update(std::string_view("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
	std::array<uint8, 20> digest = hasher.Finalize();

	char accept[32] = {};
	size_t length = Base64::Encode(std::span<char>(accept, sizeof(accept)), std::span<const uint8>(digest));
	VHAL_CHECK_EQ(std::string_view(accept, length), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}


struct TestCase {
	const char* name;
	void (*run)();
};


int main() {
	const TestCase cases[] = {
		{ "sha1-fips-abc", FipsAbc },
		{ "sha1-fips-empty", FipsEmpty },
		{ "sha1-fips-448bit", Fips448Bit },
		{ "sha1-fips-million-a", FipsMillionA },
		{ "sha1-incremental-padding-edges", IncrementalMatchesOneShot },
		{ "sha1-reset-reuse", ResetReuses },
		{ "sha1-rfc6455-accept-key", Rfc6455AcceptExample }
	};

	for (const TestCase& testCase : cases) {
		int before = VhalCheck::failCount;
		testCase.run();
		if (VhalCheck::failCount != before) {
			std::fprintf(stderr, "case failed: %s\n", testCase.name);
		}
	}

	return VhalCheck::Finish("TestSha1");
}
