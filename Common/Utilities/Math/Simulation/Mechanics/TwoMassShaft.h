#pragma once
#include <Utilities/CompileTime/Require.h>
#include <concepts>
#include <cstddef>


// An elastic shaft between a motor inertia and a load inertia, with a spring and a damper in the shaft. It replaces the Rotor in
// a chain. The states are the speed and angle of each side. The equations are the standard two-inertia model (Krause, Wasynczuk,
// Sudhoff and Pekarek, Analysis of Electric Machinery and Drive Systems, the mechanical system chapter):
//
//   Jm d(wm)/dt = Te - k (thm - thl) - c (wm - wl) - bm wm
//   Jl d(wl)/dt = k (thm - thl) + c (wm - wl) - tau_load - bl wl
//   d(thm)/dt = wm,  d(thl)/dt = wl
//
// The load side takes the constant, quadratic and road loads of the chain, and the reflected inertia of a road load. The Coulomb
// friction of the Rotor is not in this model.
namespace Simulation {


template<std::floating_point T>
class TwoMassShaft {
public:
	struct Signals {
		T speed = 0;
		T thetaM = 0;
		T loadSpeed = 0;
		T loadThetaM = 0;
	};


	static constexpr std::size_t states = 4;
	static constexpr int stage = 0;


private:
	T motorInertia = 0;
	T loadInertia = 0;
	T stiffness = 0;
	T damping = 0;
	T motorViscous = 0;
	T loadViscous = 0;
	T initialMotorAngle = 0;
	Signals latched;


public:
	constexpr TwoMassShaft() = default;


	// The motor-side inertia, kg m^2. It must be positive: the speed is divided by it.
	constexpr void SetMotorInertia(T value) {
		motorInertia = value;
	}


	// The load-side inertia, kg m^2. It must be positive: the speed is divided by it.
	constexpr void SetLoadInertia(T value) {
		loadInertia = value;
	}


	// The shaft stiffness, N m/rad.
	constexpr void SetStiffness(T value) {
		stiffness = value;
	}


	// The shaft damping, N m s/rad.
	constexpr void SetDamping(T value) {
		damping = value;
	}


	constexpr void SetMotorViscous(T value) {
		motorViscous = value;
	}


	constexpr void SetLoadViscous(T value) {
		loadViscous = value;
	}


	// The initial twist of the shaft is the initial motor angle, the load starting at zero.
	constexpr void SetInitialMotorAngle(T value) {
		initialMotorAngle = value;
	}


	// Checks the parameters when the chain is complete.
	constexpr void Validate() const {
		Require<"Simulation: the motor inertia must be positive">(motorInertia > static_cast<T>(0));
		Require<"Simulation: the load inertia must be positive">(loadInertia > static_cast<T>(0));
	}


	template<typename Bus>
	constexpr void Publish(const T* state, Bus& bus) const {
		bus.speed = state[0];
		bus.thetaM = state[1];
		bus.loadSpeed = state[2];
		bus.loadThetaM = state[3];
	}


	template<typename Bus>
	constexpr void Derivative(const T* state, const Bus& bus, T* derivative) const {
		const T twist = state[1] - state[3];
		const T twistRate = state[0] - state[2];
		const T spring = stiffness * twist + damping * twistRate;

		T totalLoadInertia = loadInertia;
		T load = 0;

		if constexpr (requires { bus.reflectedInertia; }) {
			totalLoadInertia += bus.reflectedInertia;
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

		derivative[0] = (bus.torque - spring - motorViscous * state[0]) / motorInertia;
		derivative[1] = state[0];
		derivative[2] = (spring - load - loadViscous * state[2]) / totalLoadInertia;
		derivative[3] = state[2];
	}


	constexpr void Initialize(T* state) const {
		state[0] = 0;
		state[1] = initialMotorAngle;
		state[2] = 0;
		state[3] = 0;
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latched.speed = bus.speed;
		latched.thetaM = bus.thetaM;
		latched.loadSpeed = bus.loadSpeed;
		latched.loadThetaM = bus.loadThetaM;
	}


	// The motor-side speed, rad/s, at the end of the last period.
	constexpr T Speed() const {
		return latched.speed;
	}


	// The motor-side angle, rad, at the end of the last period.
	constexpr T ThetaM() const {
		return latched.thetaM;
	}


	// The load-side speed, rad/s, at the end of the last period.
	constexpr T LoadSpeed() const {
		return latched.loadSpeed;
	}


	// The load-side angle, rad, at the end of the last period.
	constexpr T LoadThetaM() const {
		return latched.loadThetaM;
	}
};


}
