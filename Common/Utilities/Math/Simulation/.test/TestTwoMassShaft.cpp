#include "Harness.h"
#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Mechanics/TwoMassShaft.h>
#include <cmath>


// Checks of the two-mass shaft: with no torque and no friction, the twist between the two sides oscillates at
// w = sqrt(k (Jm + Jl) / (Jm Jl)) (Krause et al., the two-inertia model), and the centre of the two inertias does not move.
namespace {


using Shaft = Simulation::TwoMassShaft<double>;


struct ShaftBus : Shaft::Signals {
	double torque = 0.0;
};


}


int main() {
	// Test values: Jm = 1e-3, Jl = 4e-3, k = 100, no damping and no friction.
	const double motorInertia = 1.0e-3;
	const double loadInertia = 4.0e-3;
	const double stiffness = 100.0;
	const double omega = std::sqrt(stiffness * (motorInertia + loadInertia) / (motorInertia * loadInertia));
	const double period = 2.0 * 3.141592653589793 / omega;
	const double initialTwist = 0.1;

	Shaft shaft;
	shaft.SetMotorInertia(motorInertia);
	shaft.SetLoadInertia(loadInertia);
	shaft.SetStiffness(stiffness);
	shaft.SetInitialMotorAngle(initialTwist);

	BlockRun<Shaft, ShaftBus> run(shaft);
	const int steps = 4000;
	const double step = period / static_cast<double>(steps);

	// The twist is 0.1 cos(omega t) from rest, so it is zero at a quarter period and -0.1 at half a period.
	for (int i = 0; i < steps / 4; i++) {
		run.Step(step);
	}

	const double quarterTwist = run.state[1] - run.state[3];
	VHAL_CHECK_NEAR(quarterTwist, 0.0, 1.0e-6);

	for (int i = 0; i < steps / 4; i++) {
		run.Step(step);
	}

	const double halfTwist = run.state[1] - run.state[3];
	VHAL_CHECK_NEAR(halfTwist, -initialTwist, 1.0e-6);

	// With no net torque the momentum is zero, so Jm thm + Jl thl stays at its initial value Jm * 0.1.
	const double centre = motorInertia * run.state[1] + loadInertia * run.state[3];
	VHAL_CHECK_NEAR(centre, motorInertia * initialTwist, 1.0e-9);

	return VhalCheck::Finish("Simulation two-mass shaft");
}
