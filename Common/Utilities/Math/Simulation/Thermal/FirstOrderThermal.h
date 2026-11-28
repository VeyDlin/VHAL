#pragma once
#include <Utilities/Math/Simulation/Thermal/DelayLine.h>
#include <concepts>
#include <cstddef>


// A heater: a first-order thermal mass with a transport delay on its power input. The state is the rise of the temperature above
// the ambient temperature, theta. The heating equation is the first-order model of Astrom and Murray, Feedback Systems, with the
// input power delayed by the line (the delay is a whole number of control periods):
//
//   C d(theta)/dt = P(t - td) - theta / Rth
//   T = Tamb + theta
//
// The step response from ambient, for a power P applied at t = 0 with delay td, is
//   T(t) = Tamb + P Rth (1 - exp(-(t - td) / (Rth C))) for t >= td.
namespace Simulation {


template<std::floating_point T>
class FirstOrderThermal {
public:
	struct Signals {
		T heatPower = 0;
		T riseTemperature = 0;
	};


	static constexpr std::size_t states = 1;
	static constexpr int stage = 0;
	static constexpr std::size_t maxDelaySteps = 256;


private:
	T ambient = 0;
	T resistance = 0;
	T capacitance = 0;
	T power = 0;
	std::size_t delaySteps = 1;
	DelayLine<T, maxDelaySteps> line;
	T latchedTemperature = 0;


public:
	constexpr FirstOrderThermal() = default;


	// The ambient temperature, degrees (any unit; the output has the same unit).
	constexpr void SetAmbient(T value) {
		ambient = value;
	}


	// The thermal resistance to the ambient, K/W. It must be positive: the rise is divided by it.
	constexpr void SetResistance(T value) {
		resistance = value;
	}


	// The thermal capacitance, J/K. It must be positive: the rate is divided by it.
	constexpr void SetCapacitance(T value) {
		capacitance = value;
	}


	// The transport delay in control periods, from 1 to maxDelaySteps (the builder checks the range).
	constexpr void SetDelaySteps(std::size_t value) {
		delaySteps = value;
	}


	// The heater power, W. Settable while the plant runs: the value of one period is delayed and applied later.
	constexpr void SetPower(T value) {
		power = value;
	}


	template<typename Bus>
	constexpr void Publish(const T* state, Bus& bus) const {
		bus.heatPower = line.Delayed(delaySteps);
		bus.riseTemperature = state[0];
	}


	template<typename Bus>
	constexpr void Derivative(const T* state, const Bus& bus, T* derivative) const {
		derivative[0] = (bus.heatPower - state[0] / resistance) / capacitance;
	}


	constexpr void Initialize(T* state) const {
		state[0] = 0;
	}


	// Pushes the power of the period into the delay line.
	void Commit() {
		line.Push(power);
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latchedTemperature = ambient + bus.riseTemperature;
	}


	// The temperature at the end of the last period.
	constexpr T Temperature() const {
		return latchedTemperature;
	}
};


}
