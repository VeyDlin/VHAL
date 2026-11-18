#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdlib.h>
#include <type_traits>
#include <utility>


namespace MathUtilities {
	namespace Detail {
		// |a - b| for two values of one integer type, with the sign of a - b. The unsigned difference is exact
		// even when a - b does not fit the type, because the true magnitude is always below 2^64.
		template<typename type>
		uint64_t AbsDifference(type a, type b, bool& negative) {
			negative = a < b;
			if (negative) {
				return static_cast<uint64_t>(b) - static_cast<uint64_t>(a);
			}
			return static_cast<uint64_t>(a) - static_cast<uint64_t>(b);
		}

		// Full 64 x 64 -> 128 bit product, written with 32-bit halves so it needs no __int128 (absent on 32-bit targets).
		inline void Multiply(uint64_t a, uint64_t b, uint64_t& high, uint64_t& low) {
			const uint64_t mask = 0xFFFFFFFFu;
			uint64_t aLow = a & mask;
			uint64_t aHigh = a >> 32;
			uint64_t bLow = b & mask;
			uint64_t bHigh = b >> 32;

			uint64_t lowLow = aLow * bLow;
			uint64_t lowHigh = aLow * bHigh;
			uint64_t highLow = aHigh * bLow;
			uint64_t highHigh = aHigh * bHigh;

			uint64_t middle = (lowLow >> 32) + (lowHigh & mask) + (highLow & mask);
			low = (middle << 32) | (lowLow & mask);
			high = highHigh + (lowHigh >> 32) + (highLow >> 32) + (middle >> 32);
		}

		// Quotient of the 128-bit value (high, low) by divisor, truncated. Saturates to UINT64_MAX when the quotient
		// does not fit in 64 bits; a zero divisor also saturates instead of trapping.
		inline uint64_t DivideSaturated(uint64_t high, uint64_t low, uint64_t divisor) {
			if (high >= divisor) {
				return UINT64_MAX;
			}

			// Restoring division: the remainder stays below the divisor, so one bit of carry is enough.
			uint64_t remainder = high;
			uint64_t quotient = 0;
			for (int bit = 63; bit >= 0; --bit) {
				bool carry = (remainder >> 63) != 0;
				remainder = (remainder << 1) | ((low >> bit) & 1u);
				quotient <<= 1;
				if (carry || remainder >= divisor) {
					remainder -= divisor;
					quotient |= 1u;
				}
			}
			return quotient;
		}

		// y0 + (x2 - x0) * (y1 - y0) / (x1 - x0) for integer types of up to 64 bits, truncated toward zero and
		// saturated to the range of type. Every intermediate is exact, so the result does not depend on the range.
		template<typename type>
		type LinearIntegral(type x0, type y0, type x1, type y1, type x2) {
			static_assert(sizeof(type) <= sizeof(uint64_t), "integer types wider than 64 bits are not supported");

			bool dxNegative = false;
			bool dyNegative = false;
			bool ddNegative = false;
			uint64_t dx = AbsDifference<type>(x2, x0, dxNegative);
			uint64_t dy = AbsDifference<type>(y1, y0, dyNegative);
			uint64_t dd = AbsDifference<type>(x1, x0, ddNegative);
			bool negative = (dxNegative != dyNegative) != ddNegative;

			uint64_t high = 0;
			uint64_t low = 0;
			Multiply(dx, dy, high, low);
			uint64_t quotient = DivideSaturated(high, low, dd);

			// Room above and below y0 inside the type's range; both differences are exact in uint64.
			uint64_t headroomUp = static_cast<uint64_t>(std::numeric_limits<type>::max()) - static_cast<uint64_t>(y0);
			uint64_t headroomDown = static_cast<uint64_t>(y0) - static_cast<uint64_t>(std::numeric_limits<type>::min());

			if (!negative) {
				if (quotient > headroomUp) {
					return std::numeric_limits<type>::max();
				}
				return static_cast<type>(static_cast<uint64_t>(y0) + quotient);
			}

			if (quotient > headroomDown) {
				return std::numeric_limits<type>::min();
			}
			return static_cast<type>(static_cast<uint64_t>(y0) - quotient);
		}
	}


	template<typename type>
	type Random(type a, type b) {
		return a + rand() % (b - a + 1);
	}

	template<typename typeData, typename typeVal>
	inline void SetMin(typeData &data, typeVal val) {
		if constexpr (std::is_integral_v<typeData> && std::is_integral_v<typeVal> && !std::is_same_v<typeData, bool> && !std::is_same_v<typeVal, bool>) {
			if (std::cmp_less(val, data)) {
				data = val;
			}
		} else {
			if (val < data) {
				data = val;
			}
		}
	}

	template<typename typeData, typename typeVal>
	inline void SetMax(typeData &data, typeVal val) {
		if constexpr (std::is_integral_v<typeData> && std::is_integral_v<typeVal> && !std::is_same_v<typeData, bool> && !std::is_same_v<typeVal, bool>) {
			if (std::cmp_greater(val, data)) {
				data = val;
			}
		} else {
			if (val > data) {
				data = val;
			}
		}
	}


	template<typename type>
	type Extrapolation(type x0, type y0, type x1, type y1, type x2) {
		if constexpr (std::is_integral_v<type>) {
			return Detail::LinearIntegral<type>(x0, y0, x1, y1, x2);
		} else {
			return y0 + (((x2 - x0) * (y1 - y0)) / (x1 - x0));
		}
	}


	template<typename type>
	type Interpolation(type x0, type y0, type x1, type y1, type x2) {
		if constexpr (std::is_integral_v<type>) {
			return Detail::LinearIntegral<type>(x0, y0, x1, y1, x2);
		} else {
			return (((x2 - x0) * (y1 - y0)) / (x1 - x0)) + y0;
		}
	}
};
