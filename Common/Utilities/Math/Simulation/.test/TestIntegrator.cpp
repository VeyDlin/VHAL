#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Integrator.h>
#include <array>
#include <cmath>


// Checks of the RK4 integrator on two problems with known solutions.
namespace {


// dy/dt = -y, with the solution exp(-t).
struct Decay {
	void operator()(const double* state, double* derivative) const {
		derivative[0] = -state[0];
	}
};


// x' = v, v' = -x: the energy x^2 + v^2 is constant.
struct Oscillator {
	void operator()(const double* state, double* derivative) const {
		derivative[0] = state[1];
		derivative[1] = -state[0];
	}
};


// The error at t = 1 of the decay, integrated in the given number of steps.
double DecayError(int steps) {
	std::array<double, 1> state{ 1.0 };
	Decay decay;
	const double step = 1.0 / static_cast<double>(steps);

	for (int i = 0; i < steps; i++) {
		Simulation::Integrator<double, 1>::Step(state, step, decay);
	}

	return std::fabs(state[0] - std::exp(-1.0));
}


}


int main() {
	// Order four (Hairer, Norsett and Wanner): halving the step divides the error by about 16.
	const double coarse = DecayError(10);
	const double fine = DecayError(20);

	VHAL_CHECK(coarse > 0.0);
	VHAL_CHECK(fine > 0.0);
	VHAL_CHECK_NEAR(coarse / fine, 16.0, 1.0);

	// The decay error at 20 steps is below 1e-6 (the classic method at h = 0.05 on a unit-rate decay).
	VHAL_CHECK(fine < 1.0e-6);

	// The oscillator keeps x^2 + v^2 within 1e-6 over ten periods at 1000 steps per period.
	std::array<double, 2> state{ 1.0, 0.0 };
	Oscillator oscillator;
	const double period = 2.0 * 3.141592653589793;
	const double step = period / 1000.0;

	for (int i = 0; i < 10000; i++) {
		Simulation::Integrator<double, 2>::Step(state, step, oscillator);
	}

	VHAL_CHECK_NEAR(state[0] * state[0] + state[1] * state[1], 1.0, 1.0e-6);

	return VhalCheck::Finish("Simulation integrator");
}
