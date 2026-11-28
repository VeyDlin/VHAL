#pragma once
#include <Utilities/Math/Simulation/Angles.h>
#include <cmath>
#include <cstdint>


// A seeded generator for the sensor noise of one plant. Each instance has its own state, so two plants in one test do not
// share a sequence, and the sequence does not depend on the C library. The generator is xorshift32 (Marsaglia, Xorshift RNGs,
// Journal of Statistical Software, 2003, the 32-bit triple 13, 17, 5). The Gaussian is the Box-Muller transform (Box and Muller,
// Annals of Mathematical Statistics, 1958), using both uniform values for one sample.
namespace Simulation {


class Noise {
public:
	// The seed must not be zero. The xorshift step keeps a zero state at zero, so every Uniform() returns 0.5 / 2^32 and the
	// samples are constant, not random. The plant builder refuses a zero seed at compile time (its Seed setter).
	constexpr explicit Noise(std::uint32_t seedValue) : state(seedValue) {
	}


	// A value uniform in (0, 1).
	double Uniform() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;

		return (static_cast<double>(state) + 0.5) / 4294967296.0;
	}


	// A value with mean zero and standard deviation sigma.
	double Gaussian(double sigma) {
		const double u1 = Uniform();
		const double u2 = Uniform();
		const double radius = std::sqrt(-2.0 * std::log(u1));

		return sigma * radius * std::cos(2.0 * Pi<double>() * u2);
	}


private:
	std::uint32_t state;
};


}
