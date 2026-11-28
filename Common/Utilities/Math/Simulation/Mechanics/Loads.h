#pragma once
#include <cmath>
#include <concepts>
#include <cstddef>


// The load torques of a rotor. Each is stateless and adds its torque to the bus. The sum of the loads enters the rotor's equation
// as tau_load: J dwm/dt = Te - tau_load - ... Each load is one block, so a chain takes only the loads it uses.
namespace Simulation {


// A constant torque, N m, that the load applies against the rotation. A positive value opposes forward rotation, so a motor with
// less torque than the load runs backwards; a negative value helps forward rotation.
template<std::floating_point T>
class ConstantLoad {
public:
	struct Signals {
		T constantLoad = 0;
	};


	static constexpr std::size_t states = 0;
	static constexpr int stage = 1;


private:
	T torque = 0;


public:
	constexpr ConstantLoad() = default;


	constexpr void SetTorque(T value) {
		torque = value;
	}


	// The torque, N m.
	constexpr T Torque() const {
		return torque;
	}


	template<typename Bus>
	constexpr void Publish(const T*, Bus& bus) const {
		bus.constantLoad = torque;
	}


	template<typename Bus>
	constexpr void Derivative(const T*, const Bus&, T*) const {
	}


	constexpr void Initialize(T*) const {
	}
};


// A load that grows with the square of the speed, a fan: tau = k w |w|, N m for k in N m s^2/rad^2. The speed w is the rotor speed,
// or the load-side speed of a two-mass shaft.
template<std::floating_point T>
class QuadraticLoad {
public:
	struct Signals {
		T quadraticLoad = 0;
	};


	static constexpr std::size_t states = 0;
	static constexpr int stage = 1;


private:
	T coefficient = 0;


public:
	constexpr QuadraticLoad() = default;


	constexpr void SetCoefficient(T value) {
		coefficient = value;
	}


	constexpr T Coefficient() const {
		return coefficient;
	}


	template<typename Bus>
	constexpr void Publish(const T*, Bus& bus) const {
		T shaftSpeed = bus.speed;

		if constexpr (requires { bus.loadSpeed; }) {
			shaftSpeed = bus.loadSpeed;
		}

		bus.quadraticLoad = coefficient * shaftSpeed * std::abs(shaftSpeed);
	}


	template<typename Bus>
	constexpr void Derivative(const T*, const Bus&, T*) const {
	}


	constexpr void Initialize(T*) const {
	}
};


}
