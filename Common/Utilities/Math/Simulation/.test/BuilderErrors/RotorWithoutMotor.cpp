// expect: the rotor needs a motor
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Plant.h>


using Smallest = Simulation::Plant<+[] {
	return Simulation::Builder()
		.Add<Simulation::Rotor>()
			.Inertia(1.0e-4);
}>;


int main() {
	Smallest plant;
	return 0;
}
