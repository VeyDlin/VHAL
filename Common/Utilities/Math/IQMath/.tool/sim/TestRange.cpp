#include <Utilities/Math/NumberRange.h>
#include <cstdio>
#include <limits>


// Compile-time checks of the range trait: the largest magnitude of float, double and IQ, and the RangeAtLeast concept.
// The expected values are the IEEE limits and 2^31 / 2^Q, worked out in the comments.


static_assert(NumberRange<float>::maxAbs == static_cast<double>(std::numeric_limits<float>::max()));
static_assert(NumberRange<double>::maxAbs == std::numeric_limits<double>::max());

// 2^31 / 2^16 = 32768 and 2^31 / 2^24 = 128, the same limits as the IQ integer parts.
static_assert(NumberRange<IQ<16>>::maxAbs == 32768.0);
static_assert(NumberRange<IQ<24>>::maxAbs == 128.0);
static_assert(NumberRange<IQ<30>>::maxAbs == 2.0);
static_assert(NumberRange<IQ<24, 1024>>::maxAbs == 128.0);

// IQ<16> holds up to 32768, so a need of 32768 is met and anything above it is not.
static_assert(RangeAtLeast<IQ<16>, 32768.0>);
static_assert(!RangeAtLeast<IQ<16>, 32768.5>);
static_assert(!RangeAtLeast<IQ<24>, 1000.0>);
static_assert(RangeAtLeast<IQ<24>, 100.0>);
static_assert(RangeAtLeast<float, 1e38>);
static_assert(RangeAtLeast<double, 1e300>);

// Types without a range do not satisfy the concept, and the check does not fail with a hard error.
static_assert(!HasRange<int>);
static_assert(!RangeAtLeast<int, 1.0>);


int main() {
	std::printf("TestRange: all checks passed\n");
	return 0;
}
