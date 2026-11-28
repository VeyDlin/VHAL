#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Mechanics/Loads.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Motors/Pmsm.h>
#include <Utilities/Math/Simulation/Plant.h>
#include <cstddef>


// Open-loop scenarios of a surface PMSM on a rotor, run to steady state. A voltage step is applied on the q-axis with the d-axis
// at zero. The steady state is found in closed form: with we = p w (p = 1 here), the steady currents of the surface machine are
//
//   iq = (V - we psi) / (Rs + we^2 Ld Lq / Rs),   id = we Lq iq / Rs,
//
// and the speed is the root of Te(w) = tau_load + b w, found by bisection. The values are test values (Rs = 1 ohm, Ld = Lq = 1 mH,
// psi = 0.01 Wb, p = 1), not the parameters of a motor.
namespace {


constexpr double resistance = 1.0;
constexpr double inductance = 1.0e-3;
constexpr double flux = 0.01;
constexpr double viscous = 1.0e-4;
constexpr double voltage = 0.5;
constexpr double period = 2.0e-5;


// The steady torque at speed w (rad/s, with p = 1) and voltage V.
double SteadyTorque(double speed, double volts) {
	const double iq = (volts - speed * flux) / (resistance + speed * speed * inductance * inductance / resistance);
	return 1.5 * flux * iq;
}


// The speed where the steady torque balances the load and the viscous friction, by bisection on [low, high], where the balance
// changes sign.
double SteadySpeed(double load, double low, double high) {
	double bottom = low;
	double top = high;
	const double bottomBalance = SteadyTorque(bottom, voltage) - load - viscous * bottom;

	for (int i = 0; i < 200; i++) {
		const double middle = 0.5 * (bottom + top);
		const double balance = SteadyTorque(middle, voltage) - load - viscous * middle;

		if ((balance > 0.0) == (bottomBalance > 0.0)) {
			bottom = middle;
		} else {
			top = middle;
		}
	}

	return 0.5 * (bottom + top);
}


// The rotor inertia of the reverse-load run: 5e-5 kg m^2. The approach to the steady speed has the effective time constant
// J / (b + 1.5 psi^2 / Rs) = 5e-5 / (1e-4 + 1.5e-4) = 0.2 s: the back-EMF of the surface machine adds to the viscous friction
// (the slope of the steady torque at standstill is -1.5 psi^2 / Rs).
using Reverse = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ resistance, inductance, inductance, flux, 1 })
		.Add<Simulation::Rotor>()
			.Inertia(5.0e-5)
			.Viscous(viscous)
		.Add<Simulation::ConstantLoad>()
			.Torque(0.01)
		.Substeps(2);
}>;


// The light rotor of the spin-up run: 5e-5 kg m^2.
using Light = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ resistance, inductance, inductance, flux, 1 })
		.Add<Simulation::Rotor>()
			.Inertia(5.0e-5)
			.Viscous(viscous)
		.Substeps(2);
}>;


// The heavy rotor of the spin-up run: 20 times the light inertia, 1e-3 kg m^2, an effective time constant of 1e-3 / 2.5e-4 = 4 s.
using Heavy = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ resistance, inductance, inductance, flux, 1 })
		.Add<Simulation::Rotor>()
			.Inertia(1.0e-3)
			.Viscous(viscous)
		.Substeps(2);
}>;


}


int main() {
	// Reverse load: a 10 mN m load opposes forward rotation and the 0.5 V drive gives less than that at standstill, so the rotor
	// settles at a negative speed: the motor runs backwards. Five seconds is 25 effective time constants.
	{
		Reverse plant;
		plant.Get<Simulation::Pmsm>().SetVoltageCommand(0.0, voltage);

		const double steady = SteadySpeed(0.01, -200.0, 0.0);
		VHAL_CHECK(steady < 0.0);

		const int periods = 250000;

		for (int i = 0; i < periods; i++) {
			plant.Advance(period);
		}

		VHAL_CHECK(plant.Get<Simulation::Rotor>().Speed() < 0.0);
		VHAL_CHECK_NEAR(plant.Get<Simulation::Rotor>().Speed(), steady, 1.0e-3 * (steady < 0.0 ? -steady : steady));
	}

	// Heavy inertial load: the same voltage with no load. The heavy rotor rises more slowly than the light one, and both settle at
	// the same steady speed, the root of Te(w) = b w. The heavy run is 40 s, which is 10 effective time constants.
	{
		const double steady = SteadySpeed(0.0, 0.0, 200.0);
		VHAL_CHECK(steady > 0.0);

		Light light;
		Heavy heavy;
		light.Get<Simulation::Pmsm>().SetVoltageCommand(0.0, voltage);
		heavy.Get<Simulation::Pmsm>().SetVoltageCommand(0.0, voltage);

		const int earlyPeriods = 25000;

		for (int i = 0; i < earlyPeriods; i++) {
			light.Advance(period);
			heavy.Advance(period);
		}

		VHAL_CHECK(light.Get<Simulation::Rotor>().Speed() > 0.0);
		VHAL_CHECK(heavy.Get<Simulation::Rotor>().Speed() < light.Get<Simulation::Rotor>().Speed());

		const int latePeriods = 2000000;

		for (int i = 0; i < latePeriods; i++) {
			heavy.Advance(period);
		}

		VHAL_CHECK_NEAR(heavy.Get<Simulation::Rotor>().Speed(), steady, 1.0e-3 * steady);
	}

	return VhalCheck::Finish("Simulation scenarios");
}
