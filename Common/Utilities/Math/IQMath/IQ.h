#pragma once
#include <Utilities/DataTypes.h>
#include <concepts>
#include <limits>
#include <type_traits>


namespace IQDetail {
	constexpr int32 maxRaw = 2147483647;
	constexpr int32 minRaw = -2147483647 - 1;

	// Clamps a 64-bit intermediate to the int32 raw range.
	constexpr int32 SaturateRaw(int64 value) {
		if (value > maxRaw) {
			return maxRaw;
		}
		if (value < minRaw) {
			return minRaw;
		}
		return static_cast<int32>(value);
	}

	// Converts a real value already scaled by 2^Q to the nearest raw value, ties away from zero. NaN becomes 0, and
	// out-of-range values saturate. The arithmetic stays in T, so a float input is rounded with single-precision
	// operations, which a Cortex-M4 has in hardware.
	template<std::floating_point T>
	constexpr int32 RealToRaw(T scaled) {
		if (scaled != scaled) {
			return 0;
		}
		if (scaled >= static_cast<T>(2147483647.0)) {
			return maxRaw;
		}
		if (scaled <= static_cast<T>(-2147483648.0)) {
			return minRaw;
		}
		// The cast truncates toward zero and stays within int32, since |scaled| < 2^31. The fraction left over has no
		// more bits than scaled, so the subtraction is exact.
		int32 whole = static_cast<int32>(scaled);
		T fraction = scaled - static_cast<T>(whole);
		if (fraction >= static_cast<T>(0.5)) {
			whole++;
		} else if (fraction <= static_cast<T>(-0.5)) {
			whole--;
		}
		return whole;
	}

	// Negation that saturates to the maximum instead of overflowing for the most negative raw value.
	constexpr int32 NegateSaturated(int32 value) {
		if (value == minRaw) {
			return maxRaw;
		}
		return -value;
	}

	// Divides integers and rounds the quotient to the nearest integer, ties away from zero. A zero divisor saturates by the
	// sign of the numerator, and 0/0 gives 0.
	constexpr int32 DivideRaw(int64 numerator, int64 divisor) {
		if (divisor == 0) {
			if (numerator > 0) {
				return maxRaw;
			}
			if (numerator < 0) {
				return minRaw;
			}
			return 0;
		}
		int64 quotient = numerator / divisor;
		int64 remainder = numerator % divisor;
		// Magnitudes in unsigned arithmetic, so the comparison cannot overflow. |remainder| is below |divisor|.
		uint64 absRemainder = remainder < 0 ? 0 - static_cast<uint64>(remainder) : static_cast<uint64>(remainder);
		uint64 absDivisor = divisor < 0 ? 0 - static_cast<uint64>(divisor) : static_cast<uint64>(divisor);
		// The fraction is at least one half when |remainder| >= |divisor| - |remainder|; the step goes away from zero.
		if (absRemainder >= absDivisor - absRemainder) {
			bool negative = (numerator < 0) != (divisor < 0);
			quotient += negative ? -1 : 1;
		}
		return SaturateRaw(quotient);
	}

	// An integer scalar as int64. An unsigned value above INT64_MAX becomes INT64_MAX.
	template<std::integral S>
	constexpr int64 IntegerToInt64(S value) {
		if constexpr (std::is_unsigned_v<S>) {
			if (static_cast<uint64>(value) > static_cast<uint64>(std::numeric_limits<int64>::max())) {
				return std::numeric_limits<int64>::max();
			}
		}
		return static_cast<int64>(value);
	}

	// An integer scalar clamped to +-2^31. The clamp does not change a saturated result: beyond 2^31 the scalar already
	// moves the sum or the product outside the int32 range, and a product of two 32-bit values fits in 64 bits.
	template<std::integral S>
	constexpr int64 ScalarForSaturation(S value) {
		constexpr int64 limit = int64(1) << 31;
		int64 wide = IntegerToInt64(value);
		if (wide > limit) {
			return limit;
		}
		if (wide < -limit) {
			return -limit;
		}
		return wide;
	}

	// Clamps an integer value to the range of Target (the minimum of an unsigned type is 0).
	template<typename Target>
	constexpr Target SaturateToInteger(int64 value) {
		constexpr int64 lowest = static_cast<int64>(std::numeric_limits<Target>::min());
		if (value < lowest) {
			return std::numeric_limits<Target>::min();
		}
		if (value > 0 && static_cast<uint64>(value) > static_cast<uint64>(std::numeric_limits<Target>::max())) {
			return std::numeric_limits<Target>::max();
		}
		return static_cast<Target>(value);
	}
}


template<int Q, int LutSize = 256>
class IQ {
	static_assert(Q >= 1 && Q <= 30, "Q must be in [1, 30]");
	static_assert((LutSize & (LutSize - 1)) == 0 && LutSize >= 16, "LutSize must be a power of 2 and >= 16");

public:
	int32 raw = 0;

	static constexpr int q = Q;
	static constexpr int lutSize = LutSize;
	static constexpr int32 one = int32(1) << Q;


	constexpr IQ() = default;

	// A float or double constant is converted when the program is compiled. A run-time value does not compile: use FromFloat.
	// The scale by 2^Q is exact in float, so the product is the exact scaled value (or overflows to infinity, which saturates).
	consteval IQ(float v) : raw(IQDetail::RealToRaw(v * static_cast<float>(one))) {}
	consteval IQ(double v) : raw(IQDetail::RealToRaw(v * one)) {}
	// Every integral type converts exactly. One template covers them all, so int32 on ARM (long) is not ambiguous.
	template<std::integral S>
	constexpr IQ(S v) : raw(IQDetail::SaturateRaw(IQDetail::ScalarForSaturation(v) * one)) {}

	static constexpr IQ FromRaw(int32 r) {
		IQ result;
		result.raw = r;
		return result;
	}

	// The run-time form of IQ(float). It rounds to the nearest raw value and saturates like the constructor.
	static constexpr IQ FromFloat(float v) {
		return FromRaw(IQDetail::RealToRaw(v * static_cast<float>(one)));
	}


	constexpr float ToFloat() const {
		return static_cast<float>(raw) / one;
	}

	constexpr int32 ToInt() const {
		return raw >> Q;
	}


	// Conversion operators. Values outside the target integer type saturate to its limits.
	constexpr explicit operator float() const { return ToFloat(); }
	constexpr explicit operator int8() const { return IQDetail::SaturateToInteger<int8>(ToInt()); }
	constexpr explicit operator int16() const { return IQDetail::SaturateToInteger<int16>(ToInt()); }
	constexpr explicit operator int32() const { return ToInt(); }
	constexpr explicit operator int64() const { return static_cast<int64>(ToInt()); }
	constexpr explicit operator uint8() const { return IQDetail::SaturateToInteger<uint8>(ToInt()); }
	constexpr explicit operator uint16() const { return IQDetail::SaturateToInteger<uint16>(ToInt()); }
	constexpr explicit operator uint32() const { return IQDetail::SaturateToInteger<uint32>(ToInt()); }
	constexpr explicit operator uint64() const { return IQDetail::SaturateToInteger<uint64>(ToInt()); }


	// Arithmetic IQ <-> IQ
	constexpr IQ operator+(IQ b) const { return FromRaw(IQDetail::SaturateRaw(static_cast<int64>(raw) + b.raw)); }
	constexpr IQ operator-(IQ b) const { return FromRaw(IQDetail::SaturateRaw(static_cast<int64>(raw) - b.raw)); }
	constexpr IQ operator-() const { return FromRaw(IQDetail::NegateSaturated(raw)); }

	// Rounds to nearest; a tie (exactly half an LSB) goes toward +infinity, which costs one add before the shift.
	constexpr IQ operator*(IQ b) const {
		int64 product = static_cast<int64>(raw) * b.raw;
		return FromRaw(IQDetail::SaturateRaw((product + (int64(1) << (Q - 1))) >> Q));
	}

	constexpr IQ operator/(IQ b) const {
		int64 numerator = static_cast<int64>(raw) * (int64(1) << Q);
		return FromRaw(IQDetail::DivideRaw(numerator, b.raw));
	}

	constexpr IQ& operator+=(IQ b) { *this = *this + b; return *this; }
	constexpr IQ& operator-=(IQ b) { *this = *this - b; return *this; }
	constexpr IQ& operator*=(IQ b) { *this = *this * b; return *this; }
	constexpr IQ& operator/=(IQ b) { *this = *this / b; return *this; }


	// Arithmetic IQ <-> integer scalar. An integer scalar is exact. A float or double constant converts to IQ on the right of
	// an operator; a run-time float does not convert, so it is passed through FromFloat.
	template<std::integral S>
	constexpr IQ operator+(S s) const { return FromRaw(IQDetail::SaturateRaw(static_cast<int64>(raw) + IQDetail::ScalarForSaturation(s) * one)); }
	template<std::integral S>
	constexpr IQ operator-(S s) const { return FromRaw(IQDetail::SaturateRaw(static_cast<int64>(raw) - IQDetail::ScalarForSaturation(s) * one)); }
	template<std::integral S>
	constexpr IQ operator*(S s) const { return FromRaw(IQDetail::SaturateRaw(static_cast<int64>(raw) * IQDetail::ScalarForSaturation(s))); }
	template<std::integral S>
	constexpr IQ operator/(S s) const { return FromRaw(IQDetail::DivideRaw(raw, IQDetail::IntegerToInt64(s))); }

	template<std::integral S>
	friend constexpr IQ operator+(S s, IQ a) { return a + s; }
	template<std::integral S>
	friend constexpr IQ operator-(S s, IQ a) { return FromRaw(IQDetail::SaturateRaw(IQDetail::ScalarForSaturation(s) * one - static_cast<int64>(a.raw))); }
	template<std::integral S>
	friend constexpr IQ operator*(S s, IQ a) { return a * s; }

	template<std::integral S>
	constexpr IQ& operator+=(S s) { *this = *this + s; return *this; }
	template<std::integral S>
	constexpr IQ& operator-=(S s) { *this = *this - s; return *this; }
	template<std::integral S>
	constexpr IQ& operator*=(S s) { *this = *this * s; return *this; }
	template<std::integral S>
	constexpr IQ& operator/=(S s) { *this = *this / s; return *this; }


	// Comparison
	constexpr bool operator==(IQ b) const { return raw == b.raw; }
	constexpr bool operator!=(IQ b) const { return raw != b.raw; }
	constexpr bool operator<(IQ b) const { return raw < b.raw; }
	constexpr bool operator>(IQ b) const { return raw > b.raw; }
	constexpr bool operator<=(IQ b) const { return raw <= b.raw; }
	constexpr bool operator>=(IQ b) const { return raw >= b.raw; }


	// Conversion between IQ formats. Widening saturates; narrowing drops fractional bits (rounds toward negative infinity).
	template<int Q2, int L2>
	constexpr explicit IQ(IQ<Q2, L2> other) {
		if constexpr (Q > Q2) {
			raw = IQDetail::SaturateRaw(static_cast<int64>(other.raw) * (int64(1) << (Q - Q2)));
		} else {
			raw = other.raw >> (Q2 - Q);
		}
	}
};




// Free functions for ADL
template<int Q, int L>
constexpr IQ<Q, L> abs(IQ<Q, L> x) {
	return IQ<Q, L>::FromRaw(x.raw < 0 ? IQDetail::NegateSaturated(x.raw) : x.raw);
}

template<int Q, int L>
constexpr IQ<Q, L> fabs(IQ<Q, L> x) {
	return abs(x);
}

template<int Q, int L>
constexpr IQ<Q, L> min(IQ<Q, L> a, IQ<Q, L> b) {
	return a.raw < b.raw ? a : b;
}

template<int Q, int L>
constexpr IQ<Q, L> max(IQ<Q, L> a, IQ<Q, L> b) {
	return a.raw > b.raw ? a : b;
}

template<int Q, int L>
constexpr IQ<Q, L> clamp(IQ<Q, L> val, IQ<Q, L> lo, IQ<Q, L> hi) {
	return val.raw < lo.raw ? lo : (val.raw > hi.raw ? hi : val);
}

template<int Q, int L>
constexpr IQ<Q, L> floor(IQ<Q, L> x) {
	return IQ<Q, L>::FromRaw(x.raw & ~(IQ<Q, L>::one - 1));
}

template<int Q, int L>
constexpr IQ<Q, L> ceil(IQ<Q, L> x) {
	int32 mask = IQ<Q, L>::one - 1;
	return IQ<Q, L>::FromRaw((x.raw & mask) ? IQDetail::SaturateRaw(static_cast<int64>(x.raw | mask) + 1) : x.raw);
}

template<int Q, int L>
constexpr IQ<Q, L> round(IQ<Q, L> x) {
	return IQ<Q, L>::FromRaw(IQDetail::SaturateRaw(static_cast<int64>(x.raw) + (IQ<Q, L>::one >> 1)) & ~(IQ<Q, L>::one - 1));
}

template<int Q, int L>
constexpr IQ<Q, L> copysign(IQ<Q, L> mag, IQ<Q, L> sgn) {
	int32 a = mag.raw < 0 ? IQDetail::NegateSaturated(mag.raw) : mag.raw;
	return IQ<Q, L>::FromRaw(sgn.raw < 0 ? -a : a);
}


// Common aliases
using iq8  = IQ<8>;
using iq16 = IQ<16>;
using iq24 = IQ<24>;


template<typename T>
concept IQType = requires {
	{ T::q } -> std::convertible_to<int>;
	{ T::one } -> std::convertible_to<int32>;
	{ T::FromRaw(int32{}) } -> std::same_as<T>;
};

template<typename T>
concept RealType = std::is_floating_point_v<T> || IQType<T>;


// A run-time real value in the type T. A float or double is converted to T; an IQ takes the nearest raw value. An IQ takes a
// double through float, so the value keeps 24 significant bits before it is rounded to the raw value.
template<RealType T, std::floating_point F>
constexpr T RealFrom(F value) {
	if constexpr (IQType<T>) {
		return T::FromFloat(static_cast<float>(value));
	} else {
		return static_cast<T>(value);
	}
}
