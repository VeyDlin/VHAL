#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Angles.h>
#include <Utilities/Math/Simulation/Electrical/Inverter.h>
#include <Utilities/Math/Simulation/Mechanics/Loads.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Motors/BrushedDc.h>
#include <Utilities/Math/Simulation/Motors/Pmsm.h>
#include <Utilities/Math/Simulation/Plant.h>
#include <Utilities/Math/Simulation/Sensors/CurrentSense.h>
#include <Utilities/Math/Simulation/Sensors/Encoder.h>
#include <Utilities/Math/Simulation/Sensors/MagneticSensor.h>
#include <cmath>
#include <cstddef>
#include <cstdint>


// Checks of whole chains: the state count of a chain is the sum of its blocks' states, the inverter limit holds inside a plant,
// and a brushed motor with a constant load settles where the balance of its equations says. The brushed motor values are test
// values (R = 1 ohm, L = 1 mH, Kt = 0.05 N m/A), not those of any motor.
namespace {


using Largest = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 3.0e-3, 0.01, 4 })
		.Add<Simulation::Rotor>()
			.Inertia(1.0e-3)
			.Viscous(1.0e-4)
		.Add<Simulation::Inverter>()
			.DcBus(12.0)
		.Add<Simulation::Encoder>()
			.Counts(2048)
		.Add<Simulation::MagneticSensor>()
			.NoiseDeg(0.05)
			.Seed(5)
		.Add<Simulation::CurrentSense>()
			.OffsetA(0.002)
			.OffsetB(-0.001)
			.GainError(0.005)
		.Add<Simulation::ConstantLoad>()
			.Torque(0.0)
		.Substeps(20);
}>;


using Brushed = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::BrushedDc>(Simulation::BrushedDc<double>::Parameters{ 1.0, 1.0e-3, 0.05 })
		.Add<Simulation::Rotor>()
			.Inertia(1.0e-4)
			.Viscous(1.0e-4)
		.Add<Simulation::ConstantLoad>()
			.Torque(0.0)
		.Substeps(2);
}>;


}


int main() {
	// The state count: the PMSM has three (id, iq, thetaE), the rotor two, and the other blocks none.
	VHAL_CHECK_EQ(Largest::stateCount, static_cast<std::size_t>(5));
	VHAL_CHECK_EQ(Brushed::stateCount, static_cast<std::size_t>(3));

	// The inverter limit holds on the applied voltage: a command of (100, 0) V is applied as Vdc / sqrt(3) on the d-axis.
	{
		Largest plant;
		plant.Get<Simulation::Pmsm>().SetVoltageCommand(100.0, 0.0);
		plant.Advance(1.0e-4);

		VHAL_CHECK_NEAR(plant.Get<Simulation::Inverter>().AppliedD(), 12.0 / std::sqrt(3.0), 1.0e-9);
		VHAL_CHECK_NEAR(plant.Get<Simulation::Inverter>().AppliedQ(), 0.0, 1.0e-12);
	}

	// The encoder count, the torque and the phase current of the same plant come from the same period. Each is compared with its
	// formula written out from the plant's own latched values: the count of the rotor angle, the torque of the interior PMSM
	// (1.5 p (psi iq + (Ld - Lq) id iq)), and the phase A of the dq currents at the electrical angle (the inverse Park and Clarke
	// transforms, with the gain and the offset of the sensor). The formulas are exact, so the tolerances are of rounding.
	{
		Largest plant;
		plant.Get<Simulation::Pmsm>().SetVoltageCommand(0.0, 0.5);
		plant.Advance(1.0e-4);

		const Simulation::Pmsm<double>& motor = plant.Get<Simulation::Pmsm>();
		const double turns = Simulation::WrapAngle(plant.Get<Simulation::Rotor>().ThetaM()) / Simulation::TwoPi<double>();
		VHAL_CHECK_EQ(plant.Get<Simulation::Encoder>().Counts(), static_cast<std::uint32_t>(std::floor(turns * 2048.0)));

		const double id = motor.CurrentD();
		const double iq = motor.CurrentQ();
		const double torque = 1.5 * 4.0 * (0.01 * iq + (2.0e-3 - 3.0e-3) * id * iq);
		VHAL_CHECK_NEAR(motor.Torque(), torque, 1.0e-15);

		const double theta = motor.ThetaE();
		const double alpha = id * std::cos(theta) - iq * std::sin(theta);
		VHAL_CHECK_NEAR(plant.Get<Simulation::CurrentSense>().PhaseA(), 1.005 * alpha + 0.002, 1.0e-12);
	}

	// Brushed motor with a constant load of 0.1 N m: the balance Kt (V - Kt w) / R = b w + tau gives w = (Kt V / R - tau) /
	// (Kt^2 / R + b) = 0.2 / 0.0026 = 76.92 rad/s at 6 V. The electrical time constant is 1 ms, the mechanical one J / (Kt^2 / R + b)
	// = 0.038 s.
	{
		Brushed plant;
		plant.Get<Simulation::BrushedDc>().SetVoltageCommand(6.0);
		plant.Get<Simulation::ConstantLoad>().SetTorque(0.1);

		for (int i = 0; i < 50000; i++) {
			plant.Advance(1.0e-4);
		}

		const double expected = (0.05 * 6.0 / 1.0 - 0.1) / (0.05 * 0.05 / 1.0 + 1.0e-4);
		VHAL_CHECK_NEAR(plant.Get<Simulation::Rotor>().Speed(), expected, 1.0e-3 * expected);
	}

	return VhalCheck::Finish("Simulation chains");
}
