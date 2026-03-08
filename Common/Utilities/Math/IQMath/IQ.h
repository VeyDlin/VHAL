#pragma once
#include <Utilities/DataTypes.h>
#include <concepts>


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

	constexpr IQ(float v) : raw(static_cast<int32>(v * one)) {}
	constexpr IQ(double v) : raw(static_cast<int32>(v * one)) {}
	constexpr IQ(int v) : raw(int32(v) << Q) {}

	static constexpr IQ FromRaw(int32 r) {
		IQ result;
		result.raw = r;
		return result;
	}


	constexpr float ToFloat() const {
		return static_cast<float>(raw) / one;
	}

	constexpr double ToDouble() const {
		return static_cast<double>(raw) / one;
	}

	constexpr int32 ToInt() const {
		return raw >> Q;
	}


	// Conversion operators
	constexpr explicit operator float() const { return ToFloat(); }
	constexpr explicit operator double() const { return ToDouble(); }
	constexpr explicit operator int8() const { return static_cast<int8>(ToInt()); }
	constexpr explicit operator int16() const { return static_cast<int16>(ToInt()); }
	constexpr explicit operator int32() const { return ToInt(); }
	constexpr explicit operator int64() const { return static_cast<int64>(ToInt()); }
	constexpr explicit operator uint8() const { return static_cast<uint8>(ToInt()); }
	constexpr explicit operator uint16() const { return static_cast<uint16>(ToInt()); }
	constexpr explicit operator uint32() const { return static_cast<uint32>(ToInt()); }
	constexpr explicit operator uint64() const { return static_cast<uint64>(ToInt()); }


	// Arithmetic IQ <-> IQ
	constexpr IQ operator+(IQ b) const { return FromRaw(raw + b.raw); }
	constexpr IQ operator-(IQ b) const { return FromRaw(raw - b.raw); }
	constexpr IQ operator-() const { return FromRaw(-raw); }

	constexpr IQ operator*(IQ b) const {
		int64 tmp = static_cast<int64>(raw) * b.raw;
		return FromRaw(static_cast<int32>(tmp >> Q));
	}

	constexpr IQ operator/(IQ b) const {
		int64 tmp = static_cast<int64>(raw) << Q;
		return FromRaw(static_cast<int32>(tmp / b.raw));
	}

	constexpr IQ& operator+=(IQ b) { raw += b.raw; return *this; }
	constexpr IQ& operator-=(IQ b) { raw -= b.raw; return *this; }
	constexpr IQ& operator*=(IQ b) { *this = *this * b; return *this; }
	constexpr IQ& operator/=(IQ b) { *this = *this / b; return *this; }


	// Arithmetic IQ <-> scalar
	constexpr IQ operator+(int32 s) const { return *this + IQ(s); }
	constexpr IQ operator-(int32 s) const { return *this - IQ(s); }
	constexpr IQ operator*(int32 s) const { return FromRaw(raw * s); }
	constexpr IQ operator/(int32 s) const { return FromRaw(raw / s); }

	friend constexpr IQ operator+(int32 s, IQ a) { return IQ(s) + a; }
	friend constexpr IQ operator-(int32 s, IQ a) { return IQ(s) - a; }
	friend constexpr IQ operator*(int32 s, IQ a) { return FromRaw(a.raw * s); }


	// Comparison
	constexpr bool operator==(IQ b) const { return raw == b.raw; }
	constexpr bool operator!=(IQ b) const { return raw != b.raw; }
	constexpr bool operator<(IQ b) const { return raw < b.raw; }
	constexpr bool operator>(IQ b) const { return raw > b.raw; }
	constexpr bool operator<=(IQ b) const { return raw <= b.raw; }
	constexpr bool operator>=(IQ b) const { return raw >= b.raw; }


	// Conversion between IQ formats
	template<int Q2, int L2>
	constexpr explicit IQ(IQ<Q2, L2> other) {
		if constexpr (Q > Q2) {
			raw = other.raw << (Q - Q2);
		} else {
			raw = other.raw >> (Q2 - Q);
		}
	}
};




// Free functions for ADL
template<int Q, int L>
constexpr IQ<Q, L> abs(IQ<Q, L> x) {
	return IQ<Q, L>::FromRaw(x.raw < 0 ? -x.raw : x.raw);
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
	return IQ<Q, L>::FromRaw((x.raw & mask) ? (x.raw | mask) + 1 : x.raw);
}

template<int Q, int L>
constexpr IQ<Q, L> round(IQ<Q, L> x) {
	return IQ<Q, L>::FromRaw((x.raw + (IQ<Q, L>::one >> 1)) & ~(IQ<Q, L>::one - 1));
}

template<int Q, int L>
constexpr IQ<Q, L> copysign(IQ<Q, L> mag, IQ<Q, L> sgn) {
	int32 a = mag.raw < 0 ? -mag.raw : mag.raw;
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
