#pragma once
#include <array>
#include <concepts>
#include <cstddef>


// A transport delay on the control step. Push stores the value of one control period, and Delayed(depth) returns the value that
// was pushed depth periods ago. The buffer is a fixed array of Capacity values, so no memory is allocated. Depth must be from 1 to
// Capacity: a depth of one is the value of the previous period.
namespace Simulation {


template<std::floating_point T, std::size_t Capacity>
class DelayLine {
	static_assert(Capacity > 0, "Simulation: the delay line needs a capacity");


public:
	constexpr void Push(T value) {
		samples[next] = value;
		next = (next + 1) % Capacity;
	}


	// The value pushed depth periods ago. Before that many pushes the value is zero.
	constexpr T Delayed(std::size_t depth) const {
		return samples[(next + Capacity - depth) % Capacity];
	}


private:
	std::array<T, Capacity> samples{};
	std::size_t next = 0;
};


}
