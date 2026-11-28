#include "Harness.h"
#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Motors/Pmsm.h>
#include <cmath>


// Checks of the PMSM block against its equations (Krause et al., the dq model). The numbers are test values, chosen so that the
// time constants are a few milliseconds; they are not the parameters of any motor.
namespace {


using Motor = Simulation::Pmsm<double>;


struct PmsmBus : Motor::Signals {
	double speed = 0.0;
};


Motor::Parameters SurfaceParameters() {
	Motor::Parameters parameters;
	parameters.resistance = 0.5;
	parameters.ld = 2.0e-3;
	parameters.lq = 2.0e-3;
	parameters.psi = 0.01;
	parameters.polePairs = 4;
	return parameters;
}


Motor::Parameters InteriorParameters() {
	Motor::Parameters parameters;
	parameters.resistance = 0.5;
	parameters.ld = 2.0e-3;
	parameters.lq = 3.0e-3;
	parameters.psi = 0.01;
	parameters.polePairs = 4;
	return parameters;
}


// The electrical time constant of the surface test set, L / R = 4 ms.
constexpr double tau = 2.0e-3 / 0.5;


// Electrical time constant at standstill: id(t) = (vd / Rs) (1 - exp(-t Rs / Ld)) with we = 0 (Krause; L/R).
void CheckTimeConstant() {
	BlockRun<Motor, PmsmBus> run{ Motor(SurfaceParameters()) };
	run.inputs.speed = 0.0;
	run.block.SetVoltageCommand(1.0, 0.0);

	const int steps = 1000;
	for (int i = 0; i < steps; i++) {
		run.Step(tau / static_cast<double>(steps));
	}

	const double expected = (1.0 / 0.5) * (1.0 - std::exp(-1.0));
	VHAL_CHECK_NEAR(run.state[0], expected, 1.0e-7);
	VHAL_CHECK_NEAR(run.state[1], 0.0, 1.0e-12);
}


// Steady current with the rotor held at standstill: iq = V / Rs once the transient has decayed (fifteen time constants).
void CheckLockedCurrent() {
	BlockRun<Motor, PmsmBus> run{ Motor(SurfaceParameters()) };
	run.inputs.speed = 0.0;
	run.block.SetVoltageCommand(0.0, 1.0);

	const int steps = 15000;
	for (int i = 0; i < steps; i++) {
		run.Step(tau / 1000.0);
	}

	VHAL_CHECK_NEAR(run.state[1], 1.0 / 0.5, 1.0e-5);
	VHAL_CHECK_NEAR(run.state[0], 0.0, 1.0e-5);
}


// Back-EMF: with id = iq = 0 and vq = we psi, the q-current does not change (d iq/dt = 0), and the angle rate is we.
void CheckBackEmf() {
	BlockRun<Motor, PmsmBus> run{ Motor(SurfaceParameters()) };
	run.inputs.speed = 100.0;

	const double electricalSpeed = 4.0 * 100.0;
	run.block.SetVoltageCommand(0.0, electricalSpeed * 0.01);

	double derivative[3] = { 0.0, 0.0, 0.0 };
	const double zero[3] = { 0.0, 0.0, 0.0 };
	run.Evaluate(zero, derivative);

	VHAL_CHECK_NEAR(derivative[0], 0.0, 1.0e-9);
	VHAL_CHECK_NEAR(derivative[1], 0.0, 1.0e-9);
	VHAL_CHECK_NEAR(derivative[2], electricalSpeed, 1.0e-9);
}


// The torque output against 1.5 p (psi iq + (Ld - Lq) id iq) at several states of an interior machine.
void CheckTorque() {
	BlockRun<Motor, PmsmBus> run{ Motor(InteriorParameters()) };

	const double states[4][3] = {
		{ 0.0, 1.0, 0.1 },
		{ -2.0, 3.0, 1.2 },
		{ 1.5, -0.5, 4.0 },
		{ 4.0, 4.0, 6.0 }
	};

	for (int k = 0; k < 4; k++) {
		run.state[0] = states[k][0];
		run.state[1] = states[k][1];
		run.state[2] = states[k][2];

		const double expected = 1.5 * 4.0 * (0.01 * states[k][1] + (2.0e-3 - 3.0e-3) * states[k][0] * states[k][1]);
		VHAL_CHECK_NEAR(run.Published().torque, expected, 1.0e-12);
		VHAL_CHECK_NEAR(run.Published().thetaE, states[k][2], 1.0e-12);
	}
}


// RK4 order four on the electrical step: the error at one time constant falls by 16 when the step is halved. The error is
// C h^4 (1 + O(R h / L)): the first correction is of relative order R h / Ld, which is 0.02 at the coarse step (h = tau / 50 with
// tau = L / R), so the ratio is 16 within 0.5.
void CheckOrder() {
	const double expected = (1.0 / 0.5) * (1.0 - std::exp(-1.0));
	double errors[2] = { 0.0, 0.0 };
	const int stepCounts[2] = { 50, 100 };

	for (int k = 0; k < 2; k++) {
		BlockRun<Motor, PmsmBus> run{ Motor(SurfaceParameters()) };
		run.inputs.speed = 0.0;
		run.block.SetVoltageCommand(1.0, 0.0);

		for (int i = 0; i < stepCounts[k]; i++) {
			run.Step(tau / static_cast<double>(stepCounts[k]));
		}

		errors[k] = std::fabs(run.state[0] - expected);
	}

	VHAL_CHECK(errors[0] > 0.0);
	VHAL_CHECK(errors[1] > 0.0);
	VHAL_CHECK_NEAR(errors[0] / errors[1], 16.0, 0.5);
}


// Energy balance on a driven trajectory: the input power 1.5 (vd id + vq iq) equals the copper loss 1.5 Rs (id^2 + iq^2) plus the
// mechanical power Te wm plus the rate of the stored energy W = 0.75 (Ld id^2 + Lq iq^2), over a window of 10 ms.
// The powers are integrated with the trapezoid rule over steps of h = 1e-5 s. Its error is a sum of end terms of order h^2 times
// the rate of change of the power, so it does not grow with the window: the relative error is of order h^2 times that rate over
// the input energy, about 4e-6 in this run (measured: 3.8e-6). The tolerance is 1e-5 of the input energy.
void CheckEnergyBalance() {
	const double rs = 0.5;
	const double ld = 2.0e-3;
	const double lq = 3.0e-3;
	const double speed = 50.0;

	BlockRun<Motor, PmsmBus> run{ Motor(InteriorParameters()) };
	run.inputs.speed = speed;

	// The voltage is held over each step, as the integrator holds it, and the powers are averaged over the step's two ends.
	const double step = 1.0e-5;
	const int steps = 1000;

	double inputEnergy = 0.0;
	double copperEnergy = 0.0;
	double mechanicalEnergy = 0.0;
	const double storedStart = 0.75 * (ld * run.state[0] * run.state[0] + lq * run.state[1] * run.state[1]);

	for (int i = 0; i < steps; i++) {
		const double time = static_cast<double>(i) * step;
		const double vd = 2.0 * std::sin(2.0 * 3.141592653589793 * 800.0 * time);
		const double vq = 3.0 + 2.0 * std::cos(2.0 * 3.141592653589793 * 500.0 * time);
		run.block.SetVoltageCommand(vd, vq);

		const double startD = run.state[0];
		const double startQ = run.state[1];
		const double startCopper = 1.5 * rs * (startD * startD + startQ * startQ);
		const double startMechanical = run.Published().torque * speed;

		run.Step(step);

		const double endD = run.state[0];
		const double endQ = run.state[1];
		const double endCopper = 1.5 * rs * (endD * endD + endQ * endQ);
		const double endMechanical = run.Published().torque * speed;

		inputEnergy += step * 1.5 * (vd * 0.5 * (startD + endD) + vq * 0.5 * (startQ + endQ));
		copperEnergy += 0.5 * step * (startCopper + endCopper);
		mechanicalEnergy += 0.5 * step * (startMechanical + endMechanical);
	}

	const double storedEnd = 0.75 * (ld * run.state[0] * run.state[0] + lq * run.state[1] * run.state[1]);
	const double balance = inputEnergy - copperEnergy - mechanicalEnergy - (storedEnd - storedStart);
	VHAL_CHECK(inputEnergy > 0.0);
	VHAL_CHECK(std::fabs(balance) <= 1.0e-5 * std::fabs(inputEnergy));
}


}


int main() {
	CheckTimeConstant();
	CheckLockedCurrent();
	CheckBackEmf();
	CheckTorque();
	CheckOrder();
	CheckEnergyBalance();

	return VhalCheck::Finish("Simulation PMSM");
}
