#pragma once
#include <cmath>
#include <concepts>


namespace Simulation {


// 2 pi in T.
template<std::floating_point T>
constexpr T TwoPi() {
	return static_cast<T>(6.283185307179586476925286766559);
}


// Pi in T.
template<std::floating_point T>
constexpr T Pi() {
	return static_cast<T>(3.141592653589793238462643383280);
}


// Standard gravity, m/s^2: the conventional value, 9.80665, of the General Conference on Weights and Measures (1901).
template<std::floating_point T>
constexpr T StandardGravity() {
	return static_cast<T>(9.80665);
}


// The angle in [0, 2 pi) with the same value modulo 2 pi.
template<std::floating_point T>
T WrapAngle(T angle) {
	const T turn = TwoPi<T>();
	return angle - turn * std::floor(angle / turn);
}


// The sign of a value: -1, 0 or 1. The sign of zero is zero.
template<std::floating_point T>
constexpr T Sign(T value) {
	if (value > static_cast<T>(0)) {
		return static_cast<T>(1);
	}

	if (value < static_cast<T>(0)) {
		return static_cast<T>(-1);
	}

	return static_cast<T>(0);
}


}
