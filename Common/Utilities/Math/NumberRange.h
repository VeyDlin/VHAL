#pragma once
#include <Utilities/Math/IQMath/IQ.h>
#include <concepts>
#include <limits>
#include <type_traits>


// The largest magnitude a number type holds. A class that needs a range states it with RangeAtLeast, so a type that is too
// narrow for it fails to compile with the constraint in the message instead of saturating at run time.
template<typename T>
struct NumberRange {
};


template<std::floating_point T>
struct NumberRange<T> {
	static constexpr double maxAbs = static_cast<double>(std::numeric_limits<T>::max());
};


// An IQ<Q> holds raw values up to 2^31 - 1, so its largest magnitude is 2^31 / 2^Q.
template<int Q, int L>
struct NumberRange<IQ<Q, L>> {
	static constexpr double maxAbs = 2147483648.0 / static_cast<double>(int64(1) << Q);
};


// A type with a known range. Types without one (integers, for example) do not satisfy the concept.
template<typename T>
concept HasRange = requires {
	{ NumberRange<T>::maxAbs } -> std::convertible_to<double>;
};


// True when T holds magnitudes up to at least Need.
template<typename T, double Need>
concept RangeAtLeast = HasRange<T> && (NumberRange<T>::maxAbs >= Need);


// The real type for setup-time arithmetic: the parameters and constants that are read only when a constant is computed.
// double when T is double; float for float and for every IQ. The float path needs no double code, and an IQ constant is
// no longer a product of quantized values. Its error is a few float roundings, about 1e-7 relative, and more where the
// formula subtracts close values (about 5e-7 for Ls Lr - Lm^2 in ACIFluxEstimator).
template<typename T>
using SetupReal = std::conditional_t<std::is_same_v<T, double>, double, float>;
