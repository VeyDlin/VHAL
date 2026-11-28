#pragma once
#include <Utilities/Math/Simulation/Angles.h>
#include <Utilities/Math/Simulation/Noise.h>
#include <concepts>
#include <cstddef>
#include <cstdint>


// An absolute magnetic angle sensor: the electrical angle of the motor with a Gaussian error. The standard deviation is given in
// degrees. One sample is drawn at the end of each control period (Commit), so every RK4 step of a period sees the same error. The
// first period has no error, because the first sample is drawn at the end of it.
namespace Simulation {


template<std::floating_point T>
class MagneticSensor {
public:
	struct Signals {
		T measuredAngle = 0;
	};


	static constexpr std::size_t states = 0;
	static constexpr int stage = 2;


private:
	T noiseDegrees = 0;
	T sample = 0;
	Noise noise{ 1 };
	T latchedAngle = 0;


public:
	constexpr MagneticSensor() = default;


	// The standard deviation of the error, degrees.
	constexpr void SetNoiseDeg(T value) {
		noiseDegrees = value;
	}


	// The seed of the error sequence. It must not be zero: the builder refuses zero.
	constexpr void SetSeed(std::uint32_t value) {
		noise = Noise(value);
	}


	template<typename Bus>
	void Publish(const T*, Bus& bus) const {
		bus.measuredAngle = WrapAngle(bus.thetaE + sample);
	}


	template<typename Bus>
	constexpr void Derivative(const T*, const Bus&, T*) const {
	}


	constexpr void Initialize(T*) const {
	}


	// Draws the error of the next period.
	void Commit() {
		const double sigma = static_cast<double>(noiseDegrees) * Pi<double>() / 180.0;
		sample = static_cast<T>(noise.Gaussian(sigma));
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latchedAngle = bus.measuredAngle;
	}


	// The measured electrical angle, rad, in [0, 2 pi), at the end of the last period.
	constexpr T Angle() const {
		return latchedAngle;
	}
};


}
