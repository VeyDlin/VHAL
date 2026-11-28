#pragma once
#include <Utilities/CompileTime/Require.h>
#include <Utilities/Math/Simulation/Angles.h>
#include <cmath>
#include <concepts>
#include <cstddef>


// A pendulum on a cart, with the cart force as input. The angle is measured from the upright position, and the pole is a point
// mass at the end of a massless rod of length l. The equations are the standard cart-pendulum equations (Astrom and Murray,
// Feedback Systems, the cart-pendulum example), with D = (M + m) - m cos^2(theta):
//
//   x'' = (F + m l theta'^2 sin(theta) - m g sin(theta) cos(theta)) / D
//   theta'' = ((M + m) g sin(theta) - cos(theta) (F + m l theta'^2 sin(theta))) / (l D)
//
// The states are the cart position, velocity, the pole angle and its rate. Near the upright position the linearisation has the
// eigenvalues 0 (twice) and +/- sqrt((M + m) g / (M l)).
namespace Simulation {


template<std::floating_point T>
class CartPendulum {
public:
	struct Parameters {
		T cartMass = 0;  // M, kg, must be positive
		T poleMass = 0;  // m, kg, must be positive
		T length = 0;    // l, m, must be positive
	};


	struct Signals {
		T cartPosition = 0;
		T poleAngle = 0;
	};


	static constexpr std::size_t states = 4;
	static constexpr int stage = 0;


private:
	Parameters parameters;
	T force = 0;
	T initialPosition = 0;
	T initialAngle = 0;
	Signals latched;


public:
	constexpr CartPendulum() = default;


	constexpr explicit CartPendulum(Parameters value) : parameters(value) {
	}


	// The force on the cart, N, set while the plant runs.
	constexpr void SetForce(T value) {
		force = value;
	}


	constexpr void SetInitialPosition(T value) {
		initialPosition = value;
	}


	// The initial pole angle from upright, rad.
	constexpr void SetInitialAngle(T value) {
		initialAngle = value;
	}


	// Checks the parameters when the chain is complete.
	constexpr void Validate() const {
		Require<"Simulation: the cart mass must be positive">(parameters.cartMass > static_cast<T>(0));
		Require<"Simulation: the pole mass must be positive">(parameters.poleMass > static_cast<T>(0));
		Require<"Simulation: the pole length must be positive">(parameters.length > static_cast<T>(0));
	}


	template<typename Bus>
	constexpr void Publish(const T* state, Bus& bus) const {
		bus.cartPosition = state[0];
		bus.poleAngle = state[2];
	}


	template<typename Bus>
	constexpr void Derivative(const T* state, const Bus&, T* derivative) const {
		const T cartMass = parameters.cartMass;
		const T poleMass = parameters.poleMass;
		const T length = parameters.length;
		const T gravity = StandardGravity<T>();
		const T sine = std::sin(state[2]);
		const T cosine = std::cos(state[2]);
		const T rate = state[3];
		const T denominator = (cartMass + poleMass) - poleMass * cosine * cosine;
		const T centrifugal = poleMass * length * rate * rate * sine;

		derivative[0] = state[1];
		derivative[1] = (force + centrifugal - poleMass * gravity * sine * cosine) / denominator;
		derivative[2] = state[3];
		derivative[3] = ((cartMass + poleMass) * gravity * sine - cosine * (force + centrifugal)) / (length * denominator);
	}


	constexpr void Initialize(T* state) const {
		state[0] = initialPosition;
		state[1] = 0;
		state[2] = initialAngle;
		state[3] = 0;
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latched.cartPosition = bus.cartPosition;
		latched.poleAngle = bus.poleAngle;
	}


	// The cart position, m, at the end of the last period.
	constexpr T CartPosition() const {
		return latched.cartPosition;
	}


	// The pole angle from upright, rad, at the end of the last period.
	constexpr T PoleAngle() const {
		return latched.poleAngle;
	}
};


}
