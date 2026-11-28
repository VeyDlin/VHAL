#pragma once
#include <array>
#include <concepts>
#include <cstddef>


// Fixed-step classic Runge-Kutta of order four (Kutta 1901; Hairer, Norsett and Wanner, Solving Ordinary Differential Equations I,
// the classic fourth-order method). The state is N values of T. The derivative is a callable that takes the state and writes the
// derivative of every value:
//
//   void operator()(const T* state, T* derivative) const
//
// The callable is a template argument, so a step has no allocation and no indirection.
namespace Simulation {


template<std::floating_point T, std::size_t N>
class Integrator {
public:
	// Advances the state by one step of length step.
	template<typename Function>
	static constexpr void Step(std::array<T, N>& state, T step, Function& function) {
		std::array<T, N> k1{};
		std::array<T, N> k2{};
		std::array<T, N> k3{};
		std::array<T, N> k4{};
		std::array<T, N> probe{};
		const T half = step / static_cast<T>(2);

		function(state.data(), k1.data());

		for (std::size_t i = 0; i < N; i++) {
			probe[i] = state[i] + half * k1[i];
		}

		function(probe.data(), k2.data());

		for (std::size_t i = 0; i < N; i++) {
			probe[i] = state[i] + half * k2[i];
		}

		function(probe.data(), k3.data());

		for (std::size_t i = 0; i < N; i++) {
			probe[i] = state[i] + step * k3[i];
		}

		function(probe.data(), k4.data());

		for (std::size_t i = 0; i < N; i++) {
			state[i] += step / static_cast<T>(6) * (k1[i] + static_cast<T>(2) * k2[i] + static_cast<T>(2) * k3[i] + k4[i]);
		}
	}
};


}
