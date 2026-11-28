#include "Harness.h"
#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Motors/BrushedDc.h>
#include <cmath>


// Checks of the brushed motor's electrical part against its equation, V = R i + L di/dt + Kt w, with the rotor held at standstill
// (w = 0): the current rises with the time constant L / R to the stall current V / R. The values are test values: R = 1 ohm,
// L = 1 mH, Kt = 0.05 N m/A, V = 6 V, so the stall current is 6 A and the time constant is 1 ms.
namespace {


using Motor = Simulation::BrushedDc<double>;


struct BrushedBus : Motor::Signals {
	double speed = 0.0;
};


Motor::Parameters TestParameters() {
	Motor::Parameters parameters;
	parameters.resistance = 1.0;
	parameters.inductance = 1.0e-3;
	parameters.torqueConstant = 0.05;
	return parameters;
}


}


int main() {
	const double resistance = 1.0;
	const double inductance = 1.0e-3;
	const double voltage = 6.0;
	const double timeConstant = inductance / resistance;

	// The current from rest: i(t) = (V / R) (1 - exp(-t R / L)). The step is a hundredth of the time constant. The Runge-Kutta local
	// error is (z^5 / 120) i with z = R h / L = 0.01, so over the hundred steps of the constant the error is about 5e-10 A; the
	// tolerance of 1e-8 A is twenty times that.
	BlockRun<Motor, BrushedBus> run{ Motor(TestParameters()) };
	run.inputs.speed = 0.0;
	run.block.SetVoltageCommand(voltage);

	const int stepsPerConstant = 100;

	for (int i = 0; i < stepsPerConstant; i++) {
		run.Step(timeConstant / static_cast<double>(stepsPerConstant));
	}

	VHAL_CHECK_NEAR(run.state[0], (voltage / resistance) * (1.0 - std::exp(-1.0)), 1.0e-8);

	// The stall current: after twenty time constants the current is V / R within exp(-20) V / R = 1.2e-8 A of it, and the torque is
	// Kt V / R.
	for (int i = 0; i < 19 * stepsPerConstant; i++) {
		run.Step(timeConstant / static_cast<double>(stepsPerConstant));
	}

	VHAL_CHECK_NEAR(run.state[0], voltage / resistance, 1.0e-6 * voltage / resistance);
	VHAL_CHECK_NEAR(run.Published().torque, 0.05 * voltage / resistance, 1.0e-6 * 0.05 * voltage / resistance);

	return VhalCheck::Finish("Simulation brushed DC");
}
