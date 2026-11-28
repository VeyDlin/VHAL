#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Plant.h>
#include <Utilities/Math/Simulation/Thermal/DelayLine.h>
#include <Utilities/Math/Simulation/Thermal/FirstOrderThermal.h>
#include <cmath>
#include <cstddef>


// Checks of the heater: the step response of the first-order thermal block with a transport delay of five control periods, and
// the exact delay of the line. The values are test values: resistance 5 K/W, capacitance 10 J/K, power 10 W, ambient 25.
namespace {


using Heater = Simulation::Plant<+[] {
	return Simulation::Builder()
		.Add<Simulation::FirstOrderThermal>()
			.Ambient(25.0)
			.ThermalResistance(5.0)
			.ThermalCapacitance(10.0)
			.DelaySteps(5)
			.Power(10.0)
		.Substeps(10);
}>;


}


int main() {
	// The delay line gives the value pushed depth periods ago, and zero before that.
	{
		Simulation::DelayLine<double, 8> line;
		VHAL_CHECK_NEAR(line.Delayed(1), 0.0, 0.0);

		// After i pushes of the values 1 to i, depth 1 is the last push, depth d is the push d - 1 places before it, and a depth
		// that reaches before the first push is zero.
		for (int i = 1; i <= 20; i++) {
			line.Push(static_cast<double>(i));

			for (int depth = 1; depth <= 8; depth++) {
				const double expected = i >= depth ? static_cast<double>(i - depth + 1) : 0.0;
				VHAL_CHECK_NEAR(line.Delayed(static_cast<std::size_t>(depth)), expected, 0.0);
			}
		}
	}

	// The response: the power reaches the mass after the delay td = 5 * 0.1 s = 0.5 s, and then the rise is
	// P Rth (1 - exp(-(t - td) / (Rth C))), with Rth C = 50 s.
	Heater heater;
	const double period = 0.1;
	const double delay = 5.0 * period;
	const double timeConstant = 5.0 * 10.0;

	for (int k = 1; k <= 505; k++) {
		heater.Advance(period);

		if (k == 3 || k == 5) {
			// Before and at the delay the mass has not heated: the power of periods before the delay is zero.
			VHAL_CHECK_NEAR(heater.Get<Simulation::FirstOrderThermal>().Temperature(), 25.0, 1.0e-9);
		}
	}

	const double elapsed = 505.0 * period;
	const double rise = 10.0 * 5.0 * (1.0 - std::exp(-(elapsed - delay) / timeConstant));
	VHAL_CHECK_NEAR(heater.Get<Simulation::FirstOrderThermal>().Temperature(), 25.0 + rise, 1.0e-6);

	return VhalCheck::Finish("Simulation thermal");
}
