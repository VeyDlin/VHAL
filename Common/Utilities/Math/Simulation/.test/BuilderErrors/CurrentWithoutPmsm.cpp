// expect: the current sense needs a PMSM
#include <Utilities/Math/Simulation/Motors/BrushedDc.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Plant.h>
#include <Utilities/Math/Simulation/Sensors/CurrentSense.h>


using Smallest = Simulation::Plant<+[] {
	return Simulation::Builder()
		.Add<Simulation::BrushedDc>(Simulation::BrushedDc<double>::Parameters{ 1.0, 1.0e-3, 0.05 })
		.Add<Simulation::Rotor>()
			.Inertia(1.0e-4)
		.Add<Simulation::CurrentSense>();
}>;


int main() {
	Smallest plant;
	return 0;
}
