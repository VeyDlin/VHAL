#pragma once
#include <cmath>
#include <concepts>
#include <cstddef>


// Phase-current sensing with an offset and a gain error on each phase: the measured current is (1 + g) i + o. The phase currents
// are the true ones, from the dq currents and the electrical angle theta in radians. They are the inverse of the amplitude-invariant
// Park and Clarke transforms (Krause, Wasynczuk, Sudhoff and Pekarek, Analysis of Electric Machinery and Drive Systems, the
// amplitude-invariant transformation of the stator variables):
//
//   alpha = id cos(theta) - iq sin(theta),  beta = id sin(theta) + iq cos(theta)
//   ia = alpha,  ib = -alpha / 2 + (sqrt(3) / 2) beta,  ic = -alpha / 2 - (sqrt(3) / 2) beta
//
// A balanced set sums to zero. The transforms are written here, not taken from the FOC module, so that an error in the FOC
// transforms cannot cancel in a closed-loop test. The sensor has no state. It needs the PMSM of the chain (the builder checks it).
namespace Simulation {


template<std::floating_point T>
class CurrentSense {
public:
	struct Signals {
		T phaseA = 0;
		T phaseB = 0;
		T phaseC = 0;
	};


	static constexpr std::size_t states = 0;
	static constexpr int stage = 2;


private:
	T offsetA = 0;
	T offsetB = 0;
	T offsetC = 0;
	T gainError = 0;
	Signals latched;


public:
	constexpr CurrentSense() = default;


	// The offset of each phase sensor, A.
	constexpr void SetOffsetA(T value) {
		offsetA = value;
	}


	constexpr void SetOffsetB(T value) {
		offsetB = value;
	}


	constexpr void SetOffsetC(T value) {
		offsetC = value;
	}


	// The gain error, shared by the three phases, dimensionless.
	constexpr void SetGainError(T value) {
		gainError = value;
	}


	template<typename Bus>
	void Publish(const T*, Bus& bus) const {
		const T cosine = std::cos(bus.thetaE);
		const T sine = std::sin(bus.thetaE);
		const T alpha = bus.currentD * cosine - bus.currentQ * sine;
		const T beta = bus.currentD * sine + bus.currentQ * cosine;
		const T halfRootThree = std::sqrt(static_cast<T>(3)) / static_cast<T>(2);
		const T scale = static_cast<T>(1) + gainError;

		bus.phaseA = scale * alpha + offsetA;
		bus.phaseB = scale * (static_cast<T>(-0.5) * alpha + halfRootThree * beta) + offsetB;
		bus.phaseC = scale * (static_cast<T>(-0.5) * alpha - halfRootThree * beta) + offsetC;
	}


	template<typename Bus>
	constexpr void Derivative(const T*, const Bus&, T*) const {
	}


	constexpr void Initialize(T*) const {
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latched.phaseA = bus.phaseA;
		latched.phaseB = bus.phaseB;
		latched.phaseC = bus.phaseC;
	}


	// The measured phase currents, A, at the end of the last period.
	constexpr T PhaseA() const {
		return latched.phaseA;
	}


	constexpr T PhaseB() const {
		return latched.phaseB;
	}


	constexpr T PhaseC() const {
		return latched.phaseC;
	}
};


}
