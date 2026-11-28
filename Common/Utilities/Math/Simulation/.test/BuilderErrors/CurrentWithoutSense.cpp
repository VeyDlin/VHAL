// expect: no phase current
#include <Utilities/Math/Simulation/Motors/Pmsm.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Plant.h>


using Smallest = Simulation::Plant<+[] {
	return Simulation::Builder()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 2.0e-3, 0.01, 4 })
		.Add<Simulation::Rotor>()
			.Inertia(1.0e-4);
}>;


int main() {
	Smallest plant;
	return static_cast<int>(plant.CurrentSenseBlock().PhaseA());
}
