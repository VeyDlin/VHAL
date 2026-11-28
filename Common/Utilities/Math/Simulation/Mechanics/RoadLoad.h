#pragma once
#include <Utilities/Math/Simulation/Angles.h>
#include <cmath>
#include <concepts>
#include <cstddef>


// A vehicle on a road, as a load on the rotor. It has no state. The road force is the sum of the rolling, aerodynamic and grade
// resistances (Gillespie, Fundamentals of Vehicle Dynamics, SAE, 1992, the road-load forces of the vehicle-performance chapter).
// The rolling term takes the sign of the speed, so that it opposes the motion and is zero at standstill:
//
//   F = 1/2 rho CdA v |v| + Crr m g cos(theta) sign(v) + m g sin(theta)
//
// The wheel speed is v = w r / G, where w is the speed of the shaft the vehicle turns with: the rotor speed, or the load-side speed
// of a two-mass shaft. The torque at the motor is F r / G, and the reflected inertia is m r^2 / G^2. The grade theta is an angle
// in radians. The vehicle values are the caller's: the library holds no vehicle.
namespace Simulation {


template<std::floating_point T>
class RoadLoad {
public:
	struct Signals {
		T roadLoad = 0;
		T reflectedInertia = 0;
	};


	static constexpr std::size_t states = 0;
	static constexpr int stage = 1;


private:
	T mass = 0;
	T wheelRadius = 0;
	T gearRatio = 1;
	T rollingResistance = 0;
	T dragArea = 0;
	T airDensity = 0;
	T grade = 0;
	Signals latched;


public:
	constexpr RoadLoad() = default;


	// The vehicle mass, kg.
	constexpr void SetMass(T value) {
		mass = value;
	}


	// The wheel radius, m.
	constexpr void SetWheelRadius(T value) {
		wheelRadius = value;
	}


	// The overall gear ratio from the motor to the wheel (a motor turn gives 1/G wheel turns). It must be positive.
	constexpr void SetGearRatio(T value) {
		gearRatio = value;
	}


	// The rolling resistance coefficient, dimensionless.
	constexpr void SetRollingResistance(T value) {
		rollingResistance = value;
	}


	// The drag area C_d times A, m^2.
	constexpr void SetDragArea(T value) {
		dragArea = value;
	}


	// The air density, kg/m^3 (about 1.2 to 1.3 at sea level; the value is the caller's).
	constexpr void SetAirDensity(T value) {
		airDensity = value;
	}


	// The road grade as an angle, rad, positive uphill. Settable while the plant runs.
	constexpr void SetGrade(T value) {
		grade = value;
	}


	template<typename Bus>
	void Publish(const T*, Bus& bus) const {
		// The wheels turn with the load side of a two-mass shaft, and with the rotor otherwise.
		T shaftSpeed = bus.speed;

		if constexpr (requires { bus.loadSpeed; }) {
			shaftSpeed = bus.loadSpeed;
		}

		const T wheelSpeed = shaftSpeed * wheelRadius / gearRatio;
		const T dragForce = static_cast<T>(0.5) * airDensity * dragArea * wheelSpeed * std::abs(wheelSpeed);
		const T rollingForce = rollingResistance * mass * StandardGravity<T>() * std::cos(grade) * Sign(wheelSpeed);
		const T gradeForce = mass * StandardGravity<T>() * std::sin(grade);
		const T force = dragForce + rollingForce + gradeForce;

		bus.roadLoad = force * wheelRadius / gearRatio;
		bus.reflectedInertia = mass * wheelRadius * wheelRadius / (gearRatio * gearRatio);
	}


	template<typename Bus>
	constexpr void Derivative(const T*, const Bus&, T*) const {
	}


	constexpr void Initialize(T*) const {
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latched.roadLoad = bus.roadLoad;
		latched.reflectedInertia = bus.reflectedInertia;
	}


	// The load torque at the motor, N m, at the end of the last period.
	constexpr T LoadTorque() const {
		return latched.roadLoad;
	}


	// The reflected inertia, kg m^2, at the end of the last period.
	constexpr T ReflectedInertia() const {
		return latched.reflectedInertia;
	}
};


}
