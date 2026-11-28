#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Integrator.h>
#include <Utilities/Math/Simulation/Mechanics/Loads.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Mechanics/TwoMassShaft.h>
#include <array>
#include <cmath>


// Checks of the quadratic load, a fan with the torque k w |w|. A rotor with a fan and a constant motor torque Tm from rest has the
// closed form w(t) = a tanh(b t), with a = sqrt(Tm / k) and b = sqrt(Tm k) / J, which solves J w' = Tm - k w^2 (differentiating
// the closed form gives this equation). On a two-mass shaft the fan acts on the load side: at steady state both speeds equal
// sqrt(Tm / k) and the twist is Tm / Ks, which would be zero if the fan acted on the motor side. The values are test values.
namespace {


struct FanBus : Simulation::Rotor<double>::Signals, Simulation::QuadraticLoad<double>::Signals {
	double torque = 0.0;
};


struct FanBusTwoMass : Simulation::TwoMassShaft<double>::Signals, Simulation::QuadraticLoad<double>::Signals {
	double torque = 0.0;
};


// A rotor with a fan.
struct FanRotor {
	Simulation::Rotor<double> rotor;
	Simulation::QuadraticLoad<double> fan;
	double motorTorque = 0.0;


	void Evaluate(const double* state, double* derivative) const {
		FanBus bus;
		bus.torque = motorTorque;
		rotor.Publish(state, bus);
		fan.Publish(state, bus);
		rotor.Derivative(state, bus, derivative);
	}


	struct Evaluator {
		const FanRotor* owner;


		void operator()(const double* state, double* derivative) const {
			owner->Evaluate(state, derivative);
		}
	};
};


// A two-mass shaft with a fan on its load side.
struct FanShaft {
	Simulation::TwoMassShaft<double> shaft;
	Simulation::QuadraticLoad<double> fan;
	double motorTorque = 0.0;


	void Evaluate(const double* state, double* derivative) const {
		FanBusTwoMass bus;
		bus.torque = motorTorque;
		shaft.Publish(state, bus);
		fan.Publish(state, bus);
		shaft.Derivative(state, bus, derivative);
	}


	struct Evaluator {
		const FanShaft* owner;


		void operator()(const double* state, double* derivative) const {
			owner->Evaluate(state, derivative);
		}
	};
};


}


int main() {
	// The rotor with the fan: J = 1e-3 kg m^2, Tm = 0.05 N m, k = 1e-4 N m s^2 / rad^2, from rest, at t = 1 s.
	const double inertia = 1.0e-3;
	const double motorTorque = 0.05;
	const double coefficient = 1.0e-4;
	const double a = std::sqrt(motorTorque / coefficient);
	const double b = std::sqrt(motorTorque * coefficient) / inertia;

	{
		FanRotor plant;
		plant.rotor.SetInertia(inertia);
		plant.fan.SetCoefficient(coefficient);
		plant.motorTorque = motorTorque;

		std::array<double, 2> state{ 0.0, 0.0 };
		const double step = 1.0e-3;
		const int steps = 1000;
		FanRotor::Evaluator evaluator{ &plant };

		for (int i = 0; i < steps; i++) {
			Simulation::Integrator<double, 2>::Step(state, step, evaluator);
		}

		// The RK4 error at this step is about (b h)^4, far below the tolerance of 1e-6 of the terminal speed.
		VHAL_CHECK_NEAR(state[0], a * std::tanh(b * 1.0), 1.0e-6 * a);
	}

	// The two-mass shaft with the fan on the load side: Jm = 1e-3, Jl = 4e-3, Ks = 100 N m/rad, damping 0.5 N m s/rad. The transient
	// of the fan has a time constant of about 1 s, so 60 s is more than 50 of them.
	{
		FanShaft plant;
		plant.shaft.SetMotorInertia(1.0e-3);
		plant.shaft.SetLoadInertia(4.0e-3);
		plant.shaft.SetStiffness(100.0);
		plant.shaft.SetDamping(0.5);
		plant.fan.SetCoefficient(coefficient);
		plant.motorTorque = motorTorque;

		std::array<double, 4> state{ 0.0, 0.0, 0.0, 0.0 };
		const double step = 1.0e-4;
		const int steps = 600000;
		FanShaft::Evaluator evaluator{ &plant };

		for (int i = 0; i < steps; i++) {
			Simulation::Integrator<double, 4>::Step(state, step, evaluator);
		}

		const double steady = std::sqrt(motorTorque / coefficient);
		VHAL_CHECK_NEAR(state[0], steady, 1.0e-6 * steady);
		VHAL_CHECK_NEAR(state[2], steady, 1.0e-6 * steady);
		VHAL_CHECK_NEAR(state[1] - state[3], motorTorque / 100.0, 1.0e-6 * motorTorque / 100.0);
	}

	return VhalCheck::Finish("Simulation loads");
}
