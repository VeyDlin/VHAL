// SHA-256 of 'a' * n around the padding boundaries (55/56 and 63/64 bytes); digests from Python hashlib.
#include <VhalCheck.h>
#include <Utilities/Crypto/SHA/SHA256.h>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>


static std::string ToHex(const std::array<uint8, 32>& digest) {
	std::string text;
	for (uint8 byte : digest) {
		char pair[3];
		std::snprintf(pair, sizeof(pair), "%02x", byte);
		text += pair;
	}

	return text;
}


static void Sha256KnownDigests() {
	struct KnownDigest {
		size_t length;
		std::string hex;
	};

	const std::vector<KnownDigest> vectors = {
		{ 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
		{ 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
		{ 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" },
		{ 63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34" },
		{ 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" },
		{ 127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a" },
		{ 1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3" },
	};

	for (const KnownDigest& known : vectors) {
		std::string text(known.length, 'a');

		SHA256 hasher;
		hasher.Update(std::string_view(text));
		std::array<uint8, 32> digest = hasher.Finalize();

		VHAL_CHECK_EQ(ToHex(digest), known.hex);
	}
}


struct TestCase {
	const char* name;
	void (*run)();
};


int main() {
	const TestCase cases[] = {
		{ "sha256-known-digests", Sha256KnownDigests }
	};

	for (const TestCase& testCase : cases) {
		int before = VhalCheck::failCount;
		testCase.run();
		if (VhalCheck::failCount != before) {
			std::fprintf(stderr, "case failed: %s\n", testCase.name);
		}
	}

	return VhalCheck::Finish("TestSha256");
}
