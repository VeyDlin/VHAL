#pragma once
#include <Utilities/Math/Simulation/Angles.h>
#include <concepts>
#include <cstddef>


// A rigid rotor with viscous and Coulomb friction. The states are the speed and the angle of the shaft. The equations are
//
//   J dwm/dt = Te - tau_load - b wm - tc sign(wm)
//   d(thetaM)/dt = wm
//
// J is the rotor inertia plus the reflected inertia of a road load when one is in the chain. Each load in the chain adds its
// torque to tau_load. The Coulomb term is zero at standstill, so the model has no stiction.
namespace Simulation {


template<std::floating_point T>
class Rotor {
public:
	struct Signals {
		T speed = 0;
		T thetaM = 0;
	};


	static constexpr std::size_t states = 2;
	static constexpr int stage = 0;


private:
	T inertia = 0;
	T viscous = 0;
	T coulomb = 0;
	T initialSpeed = 0;
	T initialAngle = 0;
	Signals latched;


public:
	constexpr Rotor() = default;


	// The rotor inertia, kg m^2. The speed is divided by it, so it must be positive (the builder checks it).
	constexpr void SetInertia(T value) {
		inertia = value;
	}


	// The viscous friction coefficient, N m s.
	constexpr void SetViscous(T value) {
		viscous = value;
	}


	// The Coulomb friction torque, N m.
	constexpr void SetCoulomb(T value) {
		coulomb = value;
	}


	constexpr void SetInitialSpeed(T value) {
		initialSpeed = value;
	}


	constexpr void SetInitialAngle(T value) {
		initialAngle = value;
	}


	template<typename Bus>
	constexpr void Publish(const T* state, Bus& bus) const {
		bus.speed = state[0];
		bus.thetaM = state[1];
	}


	template<typename Bus>
	constexpr void Derivative(const T* state, const Bus& bus, T* derivative) const {
		T totalInertia = inertia;
		T load = 0;

		if constexpr (requires { bus.reflectedInertia; }) {
			totalInertia += bus.reflectedInertia;
		}

		if constexpr (requires { bus.constantLoad; }) {
			load += bus.constantLoad;
		}

		if constexpr (requires { bus.quadraticLoad; }) {
			load += bus.quadraticLoad;
		}

		if constexpr (requires { bus.roadLoad; }) {
			load += bus.roadLoad;
		}

		derivative[0] = (bus.torque - load - viscous * state[0] - coulomb * Sign(state[0])) / totalInertia;
		derivative[1] = state[0];
	}


	constexpr void Initialize(T* state) const {
		state[0] = initialSpeed;
		state[1] = initialAngle;
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latched.speed = bus.speed;
		latched.thetaM = bus.thetaM;
	}


	// The shaft speed, rad/s, at the end of the last period.
	constexpr T Speed() const {
		return latched.speed;
	}


	// The shaft angle, rad, at the end of the last period (not wrapped).
	constexpr T ThetaM() const {
		return latched.thetaM;
	}
};


}
