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

	// The Jacobian at the upright position, by central differences of the block's own derivative with no force. The states are
	// (x, x', theta, theta'). The linearisation is A[0][1] = 1, A[1][2] = -m g / M, A[2][3] = 1 and A[3][2] = (M + m) g / (M l), and
	// the other entries are zero. Its eigenvalues are 0, 0 and +/- lambda, since the matrix is block-triangular. The difference has an
	// error of order h^2 (about 1e-12) and a rounding error of about 1e-10 with h = 1e-6, so the tolerance is 1e-6.
	const double step = 1.0e-6;
	const double gravity = Simulation::StandardGravity<double>();
	double jacobian[4][4] = { { 0.0, 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0, 0.0 } };

	for (int column = 0; column < 4; column++) {
		double up[4] = { 0.0, 0.0, 0.0, 0.0 };
		double down[4] = { 0.0, 0.0, 0.0, 0.0 };
		double rateUp[4] = { 0.0, 0.0, 0.0, 0.0 };
		double rateDown[4] = { 0.0, 0.0, 0.0, 0.0 };
		up[column] = step;
		down[column] = -step;
		run.Evaluate(up, rateUp);
		run.Evaluate(down, rateDown);

		for (int row = 0; row < 4; row++) {
			jacobian[row][column] = (rateUp[row] - rateDown[row]) / (2.0 * step);
		}
	}

	VHAL_CHECK_NEAR(jacobian[0][1], 1.0, 1.0e-6);
	VHAL_CHECK_NEAR(jacobian[1][2], -parameters.poleMass * gravity / parameters.cartMass, 1.0e-6);
	VHAL_CHECK_NEAR(jacobian[2][3], 1.0, 1.0e-6);
	VHAL_CHECK_NEAR(jacobian[3][2], (parameters.cartMass + parameters.poleMass) * gravity / (parameters.cartMass * parameters.length), 1.0e-6);
	VHAL_CHECK_NEAR(jacobian[1][0], 0.0, 1.0e-6);
	VHAL_CHECK_NEAR(jacobian[3][0], 0.0, 1.0e-6);

	return VhalCheck::Finish("Simulation cart-pendulum");
}
