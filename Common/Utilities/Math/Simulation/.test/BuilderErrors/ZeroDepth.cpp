// expect: depth must be positive
#include <Utilities/Math/Simulation/Plant.h>
#include <Utilities/Math/Simulation/Thermal/FirstOrderThermal.h>


using Heater = Simulation::Plant<+[] {
	return Simulation::Builder()
		.Add<Simulation::FirstOrderThermal>()
			.ThermalResistance(5.0)
			.ThermalCapacitance(10.0)
			.DelaySteps(0);
}>;


int main() {
	Heater heater;
	return 0;
}
