#include "Harness.h"
#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Mechanics/MassSpringDamper.h>
#include <cmath>


// Free decay of a mass on a spring with a damper: x(t) = exp(-s t) (x0 cos(wd t) + (s x0 / wd) sin(wd t)) with s = c / (2 m) and
// wd = sqrt(k / m - s^2), for zero initial velocity (any damped oscillator; the textbook solution). Test values: m = 1, k = 100,
// c = 2, x0 = 1.
namespace {


using Oscillator = Simulation::MassSpringDamper<double>;


}


int main() {
	Oscillator::Parameters parameters;
	parameters.mass = 1.0;
	parameters.stiffness = 100.0;
	parameters.damping = 2.0;

	BlockRun<Oscillator, Oscillator::Signals> run{ Oscillator(parameters) };
	run.state[0] = 1.0;
	run.state[1] = 0.0;

	const int steps = 10000;
	const double duration = 1.0;

	for (int i = 0; i < steps; i++) {
		run.Step(duration / static_cast<double>(steps));
	}

	const double decay = parameters.damping / (2.0 * parameters.mass);
	const double dampedFrequency = std::sqrt(parameters.stiffness / parameters.mass - decay * decay);
	const double expected = std::exp(-decay * duration)
		* (std::cos(dampedFrequency * duration) + (decay / dampedFrequency) * std::sin(dampedFrequency * duration));

	VHAL_CHECK_NEAR(run.state[0], expected, 1.0e-7);

	return VhalCheck::Finish("Simulation mass-spring-damper");
}
