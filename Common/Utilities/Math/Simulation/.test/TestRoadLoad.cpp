#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Integrator.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Mechanics/RoadLoad.h>
#include <array>
#include <cmath>


// Checks of the road load. The formulas are the road-load equation (Ecomodder wiki). The steady speed is found by bisection of
// G Tm / r = F(v) and compared with the speed the coupled rotor reaches. The vehicle values are test values, not a vehicle.
namespace {


using Shaft = Simulation::Rotor<double>;
using Vehicle = Simulation::RoadLoad<double>;


struct CoupledBus : Shaft::Signals, Vehicle::Signals {
	double torque = 0.0;
};


constexpr double mass = 1000.0;
constexpr double wheelRadius = 0.3;
constexpr double gearRatio = 10.0;
constexpr double rollingResistance = 0.01;
constexpr double dragArea = 0.6;
constexpr double airDensity = 1.2;
constexpr double gravity = 9.80665;


// The road force of the test vehicle at speed v, flat road, v >= 0.
double RoadForce(double speed) {
	return 0.5 * airDensity * dragArea * speed * speed + rollingResistance * mass * gravity;
}


// The rotor and the road, integrated together. The rotor is integrated, the road publishes its load from the rotor speed.
struct Drivetrain {
	Shaft rotor;
	Vehicle road;
	double motorTorque = 0.0;


	void Evaluate(const double* state, double* derivative) const {
		CoupledBus bus;
		bus.torque = motorTorque;
		rotor.Publish(state, bus);
		road.Publish(state, bus);
		rotor.Derivative(state, bus, derivative);
	}


	struct Evaluator {
		const Drivetrain* owner;


		void operator()(const double* state, double* derivative) const {
			owner->Evaluate(state, derivative);
		}
	};
};


}


int main() {
	Drivetrain drivetrain;
	drivetrain.rotor.SetInertia(0.01);
	drivetrain.road.SetMass(mass);
	drivetrain.road.SetWheelRadius(wheelRadius);
	drivetrain.road.SetGearRatio(gearRatio);
	drivetrain.road.SetRollingResistance(rollingResistance);
	drivetrain.road.SetDragArea(dragArea);
	drivetrain.road.SetAirDensity(airDensity);
	drivetrain.road.SetGrade(0.0);
	drivetrain.motorTorque = 50.0;

	// Publish at speed 1000 rad/s: v = 30 m/s, the road force is F(30) and the load at the motor is F r / G.
	{
		CoupledBus bus;
		bus.torque = 0.0;
		const std::array<double, 2> state{ 1000.0, 0.0 };
		Drivetrain probe;
		probe.road = drivetrain.road;
		probe.rotor.Publish(state.data(), bus);
		probe.road.Publish(state.data(), bus);

		const double force = RoadForce(30.0);
		VHAL_CHECK_NEAR(bus.roadLoad, force * wheelRadius / gearRatio, 1.0e-9);
		VHAL_CHECK_NEAR(bus.reflectedInertia, mass * wheelRadius * wheelRadius / (gearRatio * gearRatio), 1.0e-12);
	}

	// On a grade at standstill only the grade force acts: F = m g sin(theta), and the rolling and drag terms are zero.
	{
		Vehicle graded;
		graded.SetMass(mass);
		graded.SetWheelRadius(wheelRadius);
		graded.SetGearRatio(gearRatio);
		graded.SetRollingResistance(rollingResistance);
		graded.SetDragArea(dragArea);
		graded.SetAirDensity(airDensity);
		graded.SetGrade(0.02);

		CoupledBus bus;
		const std::array<double, 2> state{ 0.0, 0.0 };
		graded.Publish(state.data(), bus);

		VHAL_CHECK_NEAR(bus.roadLoad, mass * gravity * std::sin(0.02) * wheelRadius / gearRatio, 1.0e-9);
	}

	// The steady speed: the bisection of G Tm / r = F(v) on [0, 200] m/s.
	const double wheelForce = gearRatio * drivetrain.motorTorque / wheelRadius;
	double low = 0.0;
	double high = 200.0;

	for (int i = 0; i < 200; i++) {
		const double middle = 0.5 * (low + high);

		if (RoadForce(middle) - wheelForce > 0.0) {
			high = middle;
		} else {
			low = middle;
		}
	}

	const double steadySpeed = 0.5 * (low + high);
	const double steadyRotorSpeed = steadySpeed * gearRatio / wheelRadius;

	// Integrate for 300 s at 2 ms: the slowest mode of the test vehicle has a time constant of about 21 s.
	std::array<double, 2> state{ 0.0, 0.0 };
	const double step = 2.0e-3;
	const int steps = 150000;
	Drivetrain::Evaluator evaluator{ &drivetrain };

	for (int i = 0; i < steps; i++) {
		Simulation::Integrator<double, 2>::Step(state, step, evaluator);
	}

	VHAL_CHECK(steadySpeed > 0.0);
	VHAL_CHECK_NEAR(state[0], steadyRotorSpeed, 1.0e-4 * steadyRotorSpeed);

	return VhalCheck::Finish("Simulation road load");
}
