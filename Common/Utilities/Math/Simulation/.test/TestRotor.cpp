#include "Harness.h"
#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <cmath>


// Checks of the rotor against its equations: the mechanical time constant of a coast and the linear deceleration of a Coulomb
// friction. The rotor takes its torque from a bus that has no other signal, which also checks that the optional loads are read
// only when present.
namespace {


using Shaft = Simulation::Rotor<double>;


struct ShaftBus : Shaft::Signals {
	double torque = 0.0;
};


}


int main() {
	// Coast with viscous friction only: wm(t) = wm0 exp(-t b / J). Test values: J = 1e-3, b = 2e-3, so b / J = 2 per second.
	{
		Shaft rotor;
		rotor.SetInertia(1.0e-3);
		rotor.SetViscous(2.0e-3);

		BlockRun<Shaft, ShaftBus> run(rotor);
		run.state[0] = 10.0;
		const int steps = 5000;

		for (int i = 0; i < steps; i++) {
			run.Step(0.5 / static_cast<double>(steps));
		}

		VHAL_CHECK_NEAR(run.state[0], 10.0 * std::exp(-1.0), 1.0e-7);
		VHAL_CHECK_NEAR(run.state[1], (10.0 / 2.0) * (1.0 - std::exp(-1.0)), 1.0e-6);
	}

	// Constant Coulomb friction with no torque: wm(t) = wm0 - tc t / J, a linear fall to zero. Test values: J = 1e-3, tc = 1e-3,
	// wm0 = 1, so the speed is 1 - t and reaches zero at t = 1. The check stops at 0.9 s, short of the sign change.
	{
		Shaft rotor;
		rotor.SetInertia(1.0e-3);
		rotor.SetCoulomb(1.0e-3);

		BlockRun<Shaft, ShaftBus> run(rotor);
		run.state[0] = 1.0;
		const int steps = 9000;

		for (int i = 0; i < steps; i++) {
			run.Step(0.9 / static_cast<double>(steps));
		}

		VHAL_CHECK_NEAR(run.state[0], 0.1, 1.0e-6);
	}

	// The torque drives the rotor: a constant torque from rest with no friction gives wm = (Te / J) t.
	{
		Shaft rotor;
		rotor.SetInertia(2.0e-3);

		BlockRun<Shaft, ShaftBus> run(rotor);
		run.inputs.torque = 0.004;
		const int steps = 1000;

		for (int i = 0; i < steps; i++) {
			run.Step(0.5 / static_cast<double>(steps));
		}

		VHAL_CHECK_NEAR(run.state[0], (0.004 / 2.0e-3) * 0.5, 1.0e-9);
		VHAL_CHECK_NEAR(run.state[1], 0.5 * (0.004 / 2.0e-3) * 0.5 * 0.5, 1.0e-9);
	}

	return VhalCheck::Finish("Simulation rotor");
}
