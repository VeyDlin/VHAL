// Test of the number parser of vubcheck: every number option takes its value through ParseDecimalArgument.
#include "../src/DecimalArgument.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>


namespace {
	int failures = 0;


	void Expect(std::string_view text, std::uint64_t maximum, bool expectOk, std::uint64_t expectValue) {
		std::uint64_t value = 0;
		bool ok = VubBoard::ParseDecimalArgument(text, maximum, value);
		if (ok != expectOk || (ok && value != expectValue)) {
			failures++;
			std::printf("FAIL: \"%.*s\" max %llu: ok %d value %llu, expected ok %d value %llu\n", static_cast<int>(text.size()), text.data(),
				static_cast<unsigned long long>(maximum), ok ? 1 : 0, static_cast<unsigned long long>(value), expectOk ? 1 : 0,
				static_cast<unsigned long long>(expectValue));
		}
	}
}


int main() {
	const std::uint64_t uint32Max = 4294967295ULL;
	const std::uint64_t uint64Max = 18446744073709551615ULL;

	// Plain digits, in range.
	Expect("0", 10, true, 0);
	Expect("7", 10, true, 7);
	Expect("0000012", 100, true, 12);
	Expect("200", 65536, true, 200);

	// The range limits: the maximum is accepted, one more is refused.
	Expect("86400", 86400, true, 86400);
	Expect("86401", 86400, false, 0);
	Expect("4", 4, true, 4);
	Expect("5", 4, false, 0);
	Expect("4294967295", uint32Max, true, uint32Max);
	Expect("4294967296", uint32Max, false, 0);

	// Not a plain decimal number: empty text, a sign, a space, a letter, a prefix, a fraction.
	Expect("", 10, false, 0);
	Expect("-1", 10, false, 0);
	Expect("+1", 10, false, 0);
	Expect(" 1", 10, false, 0);
	Expect("1 ", 10, false, 0);
	Expect("1x", 10, false, 0);
	Expect("abc", 10, false, 0);
	Expect("0x10", 10, false, 0);
	Expect("1.5", 10, false, 0);
	Expect("1e3", 10000, false, 0);

	// Overflow of the 64-bit value is refused, not wrapped.
	Expect("18446744073709551615", uint64Max, true, uint64Max);
	Expect("18446744073709551616", uint64Max, false, 0);
	Expect("99999999999999999999999", uint64Max, false, 0);

	// The typed form stores the value in the option's own type, and leaves it as it was on a refusal.
	std::uint32_t seed = 77;
	if (!VubBoard::ParseDecimalInto(std::string_view("4294967295"), uint32Max, seed) || seed != 4294967295U) {
		failures++;
		std::printf("FAIL: ParseDecimalInto of the 32-bit maximum\n");
	}

	if (VubBoard::ParseDecimalInto(std::string_view("4294967296"), uint32Max, seed) || seed != 4294967295U) {
		failures++;
		std::printf("FAIL: ParseDecimalInto refused a value above the 32-bit maximum but changed the option\n");
	}

	std::size_t inflight = 3;
	if (VubBoard::ParseDecimalInto(std::string_view("5"), 4, inflight) || inflight != 3) {
		failures++;
		std::printf("FAIL: ParseDecimalInto refused a value above the maximum but changed the option\n");
	}

	if (failures != 0) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}

	std::printf("all decimal argument checks passed\n");
	return 0;
}
