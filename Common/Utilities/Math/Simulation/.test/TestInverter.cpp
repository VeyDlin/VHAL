#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Electrical/Inverter.h>
#include <Utilities/Math/Simulation/Motors/Pmsm.h>
#include <cmath>


// The average inverter limits the voltage vector to Vdc / sqrt(3) (the linear range of a space-vector modulator). Inside the limit
// the command passes unchanged; beyond it the vector is scaled onto the limit circle with its direction kept.
namespace {


struct InverterBus : Simulation::Pmsm<double>::Signals {
};


}


int main() {
	const double dcBus = 12.0;
	const double limit = dcBus / std::sqrt(3.0);
	const double dummy[1] = { 0.0 };

	Simulation::Inverter<double> inverter;
	inverter.SetDcBus(dcBus);

	// Inside the limit: (1, 2) has magnitude 2.236, below 6.93.
	{
		InverterBus bus;
		bus.voltageD = 1.0;
		bus.voltageQ = 2.0;
		inverter.Publish(dummy, bus);
		inverter.Latch(bus);

		VHAL_CHECK_NEAR(bus.voltageD, 1.0, 1.0e-12);
		VHAL_CHECK_NEAR(bus.voltageQ, 2.0, 1.0e-12);
		VHAL_CHECK_NEAR(inverter.AppliedD(), 1.0, 1.0e-12);
	}

	// Beyond the limit: (6, 8) has magnitude 10, so it is scaled to magnitude Vdc / sqrt(3) and keeps the ratio 0.75.
	{
		InverterBus bus;
		bus.voltageD = 6.0;
		bus.voltageQ = 8.0;
		inverter.Publish(dummy, bus);
		inverter.Latch(bus);

		const double magnitude = std::sqrt(bus.voltageD * bus.voltageD + bus.voltageQ * bus.voltageQ);
		VHAL_CHECK_NEAR(magnitude, limit, 1.0e-9);
		VHAL_CHECK_NEAR(bus.voltageD / bus.voltageQ, 0.75, 1.0e-12);
		VHAL_CHECK_NEAR(inverter.AppliedQ(), bus.voltageQ, 1.0e-12);
	}

	// A command in the negative direction is limited the same way.
	{
		InverterBus bus;
		bus.voltageD = -20.0;
		bus.voltageQ = 0.0;
		inverter.Publish(dummy, bus);

		VHAL_CHECK_NEAR(bus.voltageD, -limit, 1.0e-9);
		VHAL_CHECK_NEAR(bus.voltageQ, 0.0, 1.0e-12);
	}

	return VhalCheck::Finish("Simulation inverter");
}
