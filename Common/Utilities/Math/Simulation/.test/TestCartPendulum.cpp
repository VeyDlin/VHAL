#include "Harness.h"
#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Mechanics/CartPendulum.h>
#include <Utilities/Math/Simulation/Angles.h>
#include <cmath>


// The cart-pendulum near the upright position. The linearisation with no force has the eigenvalues 0 (twice) and
// +/- lambda, lambda = sqrt((M + m) g / (M l)) (Astrom and Murray, Feedback Systems). From a small angle at rest the pole angle is
// theta0 cosh(lambda t). The values are test values: M = 1 kg, m = 0.1 kg, l = 0.5 m.
namespace {


using Pendulum = Simulation::CartPendulum<double>;


}


int main() {
	Pendulum::Parameters parameters;
	parameters.cartMass = 1.0;
	parameters.poleMass = 0.1;
	parameters.length = 0.5;

	const double theta0 = 1.0e-4;
	const double lambda = std::sqrt((parameters.cartMass + parameters.poleMass) * Simulation::StandardGravity<double>()
		/ (parameters.cartMass * parameters.length));

	BlockRun<Pendulum, Pendulum::Signals> run{ Pendulum(parameters) };
	run.state[2] = theta0;

	const int steps = 30000;
	const double duration = 0.3;

	for (int i = 0; i < steps; i++) {
		run.Step(duration / static_cast<double>(steps));
	}

	// The nonlinear terms are of order theta0^2, far below the tolerance of 1e-3 relative.
	VHAL_CHECK_NEAR(run.state[2], theta0 * std::cosh(lambda * duration), 1.0e-3 * theta0 * std::cosh(lambda * duration));

	// The linearised cart accelerates as x'' = -m g theta / M while the pole leans, so its velocity is negative after the start.
	VHAL_CHECK(run.state[1] < 0.0);

	return VhalCheck::Finish("Simulation cart-pendulum");
}
