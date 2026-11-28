// expect: a plant is float or double
#include <Utilities/Math/IQMath/IQ.h>
#include <Utilities/Math/Simulation/Plant.h>


using Smallest = Simulation::Plant<+[] {
	return Simulation::Builder<IQ<24>>();
}>;


int main() {
	Smallest plant;
	return 0;
}
