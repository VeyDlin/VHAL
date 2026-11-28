#pragma once
#include <Utilities/CompileTime/Require.h>
#include <concepts>
#include <cstddef>


// A mass on a spring with a damper, driven by a force that the caller sets while the plant runs:
//
//   m x'' + c x' + k x = F(t)
//
// The states are the position and the velocity. It is a standalone chain: it does not take part in the motor chain.
namespace Simulation {


template<std::floating_point T>
class MassSpringDamper {
public:
	struct Parameters {
		T mass = 0;       // kg, must be positive
		T stiffness = 0;  // N/m
		T damping = 0;    // N s/m
	};


	struct Signals {
		T position = 0;
		T velocity = 0;
	};


	static constexpr std::size_t states = 2;
	static constexpr int stage = 0;


private:
	Parameters parameters;
	T force = 0;
	T initialPosition = 0;
	T initialVelocity = 0;
	Signals latched;


public:
	constexpr MassSpringDamper() = default;


	constexpr explicit MassSpringDamper(Parameters value) : parameters(value) {
	}


	// The external force F, N, set while the plant runs.
	constexpr void SetForce(T value) {
		force = value;
	}


	constexpr void SetInitialPosition(T value) {
		initialPosition = value;
	}


	constexpr void SetInitialVelocity(T value) {
		initialVelocity = value;
	}


	// Checks the parameters when the chain is complete.
	constexpr void Validate() const {
		Require<"Simulation: the mass must be positive">(parameters.mass > static_cast<T>(0));
	}


	template<typename Bus>
	constexpr void Publish(const T* state, Bus& bus) const {
		bus.position = state[0];
		bus.velocity = state[1];
	}


	template<typename Bus>
	constexpr void Derivative(const T* state, const Bus&, T* derivative) const {
		derivative[0] = state[1];
		derivative[1] = (force - parameters.damping * state[1] - parameters.stiffness * state[0]) / parameters.mass;
	}


	constexpr void Initialize(T* state) const {
		state[0] = initialPosition;
		state[1] = initialVelocity;
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latched.position = bus.position;
		latched.velocity = bus.velocity;
	}


	// The position, m, at the end of the last period.
	constexpr T Position() const {
		return latched.position;
	}


	// The velocity, m/s, at the end of the last period.
	constexpr T Velocity() const {
		return latched.velocity;
	}
};


}
