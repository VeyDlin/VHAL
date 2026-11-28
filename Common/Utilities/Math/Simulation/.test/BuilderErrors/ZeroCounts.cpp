// expect: counts must be positive
#include <Utilities/Math/Simulation/Plant.h>
#include <Utilities/Math/Simulation/Sensors/Encoder.h>
#include <Utilities/Math/Simulation/Motors/Pmsm.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>


using Smallest = Simulation::Plant<+[] {
	return Simulation::Builder()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 2.0e-3, 0.01, 4 })
		.Add<Simulation::Rotor>()
			.Inertia(1.0e-4)
		.Add<Simulation::Encoder>()
			.Counts(0);
}>;


int main() {
	Smallest plant;
	return 0;
}
