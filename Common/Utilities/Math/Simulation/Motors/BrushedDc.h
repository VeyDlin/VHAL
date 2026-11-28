#pragma once
#include <Utilities/CompileTime/Require.h>
#include <concepts>
#include <cstddef>


// Brushed DC motor, electrical part. The state is the armature current. The equations (Northwestern University, Brushed DC Motor
// Theory, https://hades.mech.northwestern.edu/index.php/Brushed_DC_Motor_Theory):
//
//   V = R i + L di/dt + Ke w
//   torque = Kt i, with Kt = Ke in SI units
//
// The mechanical part is a Rotor, which takes the torque from the bus. The brushed motor has no separate speed state.
namespace Simulation {


template<std::floating_point T>
class BrushedDc {
public:
	struct Parameters {
		T resistance = 0;      // armature resistance, ohm
		T inductance = 0;      // armature inductance, H
		T torqueConstant = 0;  // Kt, N m/A, which is also the back-EMF constant Ke, V s/rad
	};


	struct Signals {
		T voltage = 0;
		T current = 0;
		T torque = 0;
	};


	static constexpr std::size_t states = 1;
	static constexpr int stage = 0;


private:
	Parameters parameters;
	T command = 0;
	Signals latched;


public:
	constexpr BrushedDc() = default;


	constexpr explicit BrushedDc(Parameters value) : parameters(value) {
	}


	// The terminal voltage command, V.
	constexpr void SetVoltageCommand(T value) {
		command = value;
	}


	// Checks the parameters when the chain is complete.
	constexpr void Validate() const {
		Require<"Simulation: the armature inductance must be positive">(parameters.inductance > static_cast<T>(0));
		Require<"Simulation: the torque constant must be positive">(parameters.torqueConstant > static_cast<T>(0));
	}


	template<typename Bus>
	constexpr void Publish(const T* state, Bus& bus) const {
		bus.voltage = command;
		bus.current = state[0];
		bus.torque = parameters.torqueConstant * state[0];
	}


	template<typename Bus>
	constexpr void Derivative(const T* state, const Bus& bus, T* derivative) const {
		derivative[0] = (bus.voltage - parameters.resistance * state[0] - parameters.torqueConstant * bus.speed) / parameters.inductance;
	}


	constexpr void Initialize(T*) const {
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latched.voltage = bus.voltage;
		latched.current = bus.current;
		latched.torque = bus.torque;
	}


	// The armature current, A, at the end of the last period.
	constexpr T Current() const {
		return latched.current;
	}


	// The torque, N m, at the end of the last period.
	constexpr T Torque() const {
		return latched.torque;
	}
};


}
