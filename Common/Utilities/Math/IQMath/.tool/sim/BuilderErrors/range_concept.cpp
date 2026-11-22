// expect: constraints not satisfied
#include <Utilities/Math/NumberRange.h>


// A class that needs magnitudes up to 40000 cannot be an IQ<16>, whose largest magnitude is 32768.
template<typename T>
requires RangeAtLeast<T, 40000.0>
struct NeedsRange {
};


NeedsRange<IQ<16>> narrow;
