#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Integrator.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Mechanics/RoadLoad.h>
#include <Utilities/Math/Simulation/Mechanics/TwoMassShaft.h>
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


// The two-mass shaft with the road on its load side, accelerating from rest on a grade with rolling and drag resistance. The
// reference is the same system written out in this file, with its own right-hand side and its own step. The road force takes the
// load-side speed, so a road that read the motor speed would differ from the reference by more than the tolerance.
namespace {


constexpr double motorInertia = 0.01;
constexpr double loadInertia = 0.02;
constexpr double shaftStiffness = 2000.0;
constexpr double motorTorqueStep = 50.0;
constexpr double roadGrade = 0.02;
constexpr double reflectedInertia = mass * wheelRadius * wheelRadius / (gearRatio * gearRatio);


// The road torque at the motor-referred load side, F r / G, with F from the road-load equation. The rolling term has the sign of
// the speed, which is zero at standstill.
double ReferenceRoadTorque(double loadSpeed) {
	const double wheelSpeed = loadSpeed * wheelRadius / gearRatio;
	double direction = 0.0;

	if (wheelSpeed > 0.0) {
		direction = 1.0;
	} else if (wheelSpeed < 0.0) {
		direction = -1.0;
	}

	const double force = 0.5 * airDensity * dragArea * wheelSpeed * std::fabs(wheelSpeed)
		+ rollingResistance * mass * gravity * std::cos(roadGrade) * direction
		+ mass * gravity * std::sin(roadGrade);

	return force * wheelRadius / gearRatio;
}


// The reference system. The state is the motor speed, the motor angle, the load speed and the load angle.
struct ReferenceSystem {
	void operator()(const double* state, double* derivative) const {
		const double twist = state[1] - state[3];

		derivative[0] = (motorTorqueStep - shaftStiffness * twist) / motorInertia;
		derivative[1] = state[0];
		derivative[2] = (shaftStiffness * twist - ReferenceRoadTorque(state[2])) / (loadInertia + reflectedInertia);
		derivative[3] = state[2];
	}
};


struct ShaftRoadBus : Simulation::TwoMassShaft<double>::Signals, Vehicle::Signals {
	double torque = 0.0;
};


// The blocks: the two-mass shaft publishes the load speed, and the road reads it at the same stage.
struct AccelerationPlant {
	Simulation::TwoMassShaft<double> shaft;
	Vehicle road;


	void Evaluate(const double* state, double* derivative) const {
		ShaftRoadBus bus;
		bus.torque = motorTorqueStep;
		shaft.Publish(state, bus);
		road.Publish(state, bus);
		shaft.Derivative(state, bus, derivative);
	}


	struct Evaluator {
		const AccelerationPlant* owner;


		void operator()(const double* state, double* derivative) const {
			owner->Evaluate(state, derivative);
		}
	};
};


// The tolerance of 1e-4 rad/s. The classic Runge-Kutta error of the plant step is (w h)^4 w t / 120 times the amplitude of the
// oscillation, with w = 450 rad/s, h = 2.5e-5 s, t = 1 s and an amplitude of about 12 rad/s: about 1e-6 rad/s. The rolling term
// has a sign at zero speed, and the start passes through zero speed, which adds about 1e-5 rad/s. A road that read the motor speed
// differs from the reference by about 0.3 rad/s, so the check separates the two.
void CheckTwoMassAcceleration() {
	AccelerationPlant plant;
	plant.shaft.SetMotorInertia(motorInertia);
	plant.shaft.SetLoadInertia(loadInertia);
	plant.shaft.SetStiffness(shaftStiffness);
	plant.road.SetMass(mass);
	plant.road.SetWheelRadius(wheelRadius);
	plant.road.SetGearRatio(gearRatio);
	plant.road.SetRollingResistance(rollingResistance);
	plant.road.SetDragArea(dragArea);
	plant.road.SetAirDensity(airDensity);
	plant.road.SetGrade(roadGrade);

	std::array<double, 4> state{ 0.0, 0.0, 0.0, 0.0 };
	const double step = 2.5e-5;
	const int steps = 40000;
	AccelerationPlant::Evaluator evaluator{ &plant };

	for (int i = 0; i < steps; i++) {
		Simulation::Integrator<double, 4>::Step(state, step, evaluator);
	}

	std::array<double, 4> reference{ 0.0, 0.0, 0.0, 0.0 };
	const ReferenceSystem system;
	const double referenceStep = 1.0e-5;
	const int referenceSteps = 100000;

	for (int i = 0; i < referenceSteps; i++) {
		Simulation::Integrator<double, 4>::Step(reference, referenceStep, system);
	}

	VHAL_CHECK(reference[2] > 0.0);
	VHAL_CHECK_NEAR(state[0], reference[0], 1.0e-4);
	VHAL_CHECK_NEAR(state[2], reference[2], 1.0e-4);
	VHAL_CHECK_NEAR(state[3], reference[3], 1.0e-4);
}


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

	CheckTwoMassAcceleration();

	return VhalCheck::Finish("Simulation road load");
}
