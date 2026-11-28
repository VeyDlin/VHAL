#pragma once
#include <Utilities/Math/FOC/ClarkeTransformation.h>
#include <Utilities/Math/FOC/ParkTransformation.h>
#include <concepts>
#include <cstddef>


// Phase-current sensing with an offset and a gain error on each phase: the measured current is (1 + g) i + o. The phase currents
// are the true ones, from the dq currents and the electrical angle through the library's inverse Park and inverse Clarke
// transforms (the amplitude-invariant form, so a balanced set sums to zero). The sensor has no state.
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
		ParkTransformation<T> park;
		park.SetPark(typename ParkTransformation<T>::Park{ bus.currentD, bus.currentQ, bus.thetaE });
		park.ResolveInverse();

		const typename ParkTransformation<T>::Axis axis = park.GetAxis();

		ClarkeTransformation<T> clarke;
		clarke.SetClarke(typename ClarkeTransformation<T>::Clarke{ axis.alpha, axis.beta });
		clarke.ResolveInverse();

		const typename ClarkeTransformation<T>::Phase phase = clarke.GetPhase();
		const T scale = static_cast<T>(1) + gainError;

		bus.phaseA = scale * phase.a + offsetA;
		bus.phaseB = scale * phase.b + offsetB;
		bus.phaseC = scale * phase.c + offsetC;
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
