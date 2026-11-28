// Every snippet of the Simulation wiki pages compiles here, and the checks of the snippets that run hold. Each page's snippets have
// their own namespace, because they reuse names. The snippets are the code of the pages, copied as they are. The checks compare with
// reference values: the initial values of the blocks, the formulas of the first steps, and the closed forms of the blocks.
#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Angles.h>
#include <Utilities/Math/Simulation/Electrical/Inverter.h>
#include <Utilities/Math/Simulation/Integrator.h>
#include <Utilities/Math/Simulation/Mechanics/CartPendulum.h>
#include <Utilities/Math/Simulation/Mechanics/Loads.h>
#include <Utilities/Math/Simulation/Mechanics/MassSpringDamper.h>
#include <Utilities/Math/Simulation/Mechanics/RoadLoad.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Mechanics/TwoMassShaft.h>
#include <Utilities/Math/Simulation/Motors/BrushedDc.h>
#include <Utilities/Math/Simulation/Motors/Pmsm.h>
#include <Utilities/Math/Simulation/Noise.h>
#include <Utilities/Math/Simulation/Plant.h>
#include <Utilities/Math/Simulation/Sensors/CurrentSense.h>
#include <Utilities/Math/Simulation/Sensors/Encoder.h>
#include <Utilities/Math/Simulation/Sensors/MagneticSensor.h>
#include <Utilities/Math/Simulation/Thermal/DelayLine.h>
#include <Utilities/Math/Simulation/Thermal/FirstOrderThermal.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <concepts>


// The exact comparison of two values: a double is compared exactly through a check with no tolerance, and an integer with VHAL_CHECK_EQ.
namespace {


void CheckEqual(double value, double expected) {
	VHAL_CHECK_NEAR(value, expected, 0.0);
}


template<std::integral Integer>
void CheckEqual(Integer value, Integer expected) {
	VHAL_CHECK_EQ(value, expected);
}


}


namespace PagePlant {


// The smallest chain: a PMSM with an ideal rotor and an average inverter.
using Smallest = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 2.0e-3, 0.01, 4 })
		.Add<Simulation::Rotor>()
			.Inertia(2.0e-5)
			.Viscous(1.0e-6)
		.Add<Simulation::Inverter>()
			.DcBus(14.8)
		.Substeps(20);
}>;


// A chain with every kind of sensor and load, and a two-mass shaft in place of the rotor.
using Largest = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 3.0e-3, 0.01, 4 })
		.Add<Simulation::TwoMassShaft>()
			.MotorInertia(1.0e-4)
			.LoadInertia(4.0e-4)
			.Stiffness(100.0)
			.Damping(0.01)
		.Add<Simulation::RoadLoad>()
			.Mass(1000.0)
			.WheelRadius(0.3)
			.GearRatio(10.0)
			.RollingResistance(0.01)
			.DragArea(0.6)
			.AirDensity(1.2)
			.Grade(0.0)
		.Add<Simulation::ConstantLoad>()
			.Torque(0.0)
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
			.OffsetC(0.0)
			.GainError(0.005)
		.Substeps(40);
}>;


void Run() {
	// One period of 100 us with a voltage of 1 V on the q-axis, from rest. The electrical time constant L / R is 4 ms, far longer than
	// the period, and the back-EMF is small over it: the q current is (V / R) (1 - exp(-t R / L)) to a relative 1e-3, and the speed is
	// the integral of the torque 1.5 p psi iq over the inertia. The inverter does not limit 1 V, so the applied voltage is 1 V.
	Smallest plant;
	plant.Get<Simulation::Pmsm>().SetVoltageCommand(0.0, 1.0);
	plant.Advance(100.0e-6);

	const double time = 100.0e-6;
	const double tau = 2.0e-3 / 0.5;
	const double current = (1.0 / 0.5) * (1.0 - std::exp(-time / tau));
	const double currentIntegral = (1.0 / 0.5) * (time - tau * (1.0 - std::exp(-time / tau)));
	const double expectedSpeed = 1.5 * 4.0 * 0.01 / 2.0e-5 * currentIntegral;

	VHAL_CHECK_NEAR(plant.Get<Simulation::Pmsm>().CurrentQ(), current, 1.0e-3 * current);
	VHAL_CHECK_NEAR(plant.Get<Simulation::Pmsm>().Torque(), 1.5 * 4.0 * 0.01 * plant.Get<Simulation::Pmsm>().CurrentQ(), 1.0e-15);
	VHAL_CHECK_NEAR(plant.Get<Simulation::Rotor>().Speed(), expectedSpeed, 1.0e-3 * expectedSpeed);
	VHAL_CHECK(plant.Get<Simulation::Rotor>().ThetaM() > 0.0);
	VHAL_CHECK(plant.Get<Simulation::Rotor>().ThetaM() < plant.Get<Simulation::Rotor>().Speed() * time);
	VHAL_CHECK_NEAR(plant.Get<Simulation::Inverter>().AppliedQ(), 1.0, 1.0e-12);
	CheckEqual(Smallest::stateCount, static_cast<std::size_t>(5));

	// The largest chain at rest with no command: no current, no torque and no motion, so the sensors read their offsets and zero.
	Largest large;
	large.Advance(100.0e-6);
	CheckEqual(large.Get<Simulation::TwoMassShaft>().LoadSpeed(), 0.0);
	CheckEqual(large.Road().LoadTorque(), 0.0);
	CheckEqual(large.EncoderBlock().Counts(), 0u);
	VHAL_CHECK_NEAR(large.CurrentSenseBlock().PhaseA(), 0.002, 1.0e-12);
	VHAL_CHECK_NEAR(large.CurrentSenseBlock().PhaseB(), -0.001, 1.0e-12);

	// The magnetic sensor reads the electrical angle, zero, with its Gaussian error of 0.05 degrees (about 8.7e-4 rad). The error is
	// within six standard deviations of zero, which holds for all but one sample in 10^9.
	const double sigma = 0.05 * Simulation::Pi<double>() / 180.0;
	double error = large.Get<Simulation::MagneticSensor>().Angle();

	if (error > Simulation::Pi<double>()) {
		error -= Simulation::TwoPi<double>();
	}

	VHAL_CHECK(std::fabs(error) < 6.0 * sigma);
}


}


namespace PagePlantMembers {


void Run() {
	constexpr auto chain = Simulation::PlantBuilder<double>().Add<Simulation::Rotor>().Inertia(1.0e-4).Substeps(20);
	VHAL_CHECK(chain.substepCount == 20u);

	using Plant = Simulation::Plant<+[] {
		return Simulation::PlantBuilder<double>()
			.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 2.0e-3, 0.01, 4 })
			.Add<Simulation::Rotor>()
				.Inertia(2.0e-5)
				.Viscous(1.0e-6)
			.Add<Simulation::Inverter>()
				.DcBus(14.8)
			.Substeps(20);
	}>;

	// No command: the rotor stays at rest.
	Plant plant;
	plant.Advance(100.0e-6);
	Simulation::Rotor<double>& rotor = plant.Get<Simulation::Rotor>();
	CheckEqual(rotor.Speed(), 0.0);
}


}


namespace PageInteger {


void Run() {
	VHAL_CHECK_NEAR(Simulation::WrapAngle(-0.5), Simulation::TwoPi<double>() - 0.5, 1.0e-12);
	CheckEqual(Simulation::Sign(-3.0), -1.0);
	CheckEqual(Simulation::Sign(0.0), 0.0);
	VHAL_CHECK_NEAR(Simulation::TwoPi<double>(), 6.283185307179586, 1.0e-15);
	VHAL_CHECK_NEAR(Simulation::Pi<double>(), 3.141592653589793, 1.0e-15);
	CheckEqual(Simulation::StandardGravity<double>(), 9.80665);
}


}


namespace PageIntegrator {


struct Decay {
	void operator()(const double* state, double* derivative) const {
		derivative[0] = -state[0];
	}
};


void Run() {
	// One RK4 step of dy/dt = -y from 1 is the stability polynomial 1 - z + z^2/2 - z^3/6 + z^4/24 with z = 0.1.
	std::array<double, 1> state{ 1.0 };
	Decay decay;
	Simulation::Integrator<double, 1>::Step(state, 0.1, decay);
	const double z = 0.1;
	VHAL_CHECK_NEAR(state[0], 1.0 - z + z * z / 2.0 - z * z * z / 6.0 + z * z * z * z / 24.0, 1.0e-15);
}


}


namespace PageNoise {


// The xorshift32 step of the generator, written out, to give the first values of seed 1234.
std::uint32_t XorshiftStep(std::uint32_t state) {
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}


void Run() {
	// The first Uniform is the first step of the sequence, (x + 0.5) / 2^32. The Gaussian takes the next two uniforms (Box-Muller).
	const std::uint32_t first = XorshiftStep(1234);
	const std::uint32_t second = XorshiftStep(first);
	const std::uint32_t third = XorshiftStep(second);
	const double u1 = (static_cast<double>(second) + 0.5) / 4294967296.0;
	const double u2 = (static_cast<double>(third) + 0.5) / 4294967296.0;

	Simulation::Noise noise(1234);
	VHAL_CHECK_NEAR(noise.Uniform(), (static_cast<double>(first) + 0.5) / 4294967296.0, 1.0e-15);
	VHAL_CHECK_NEAR(noise.Gaussian(0.5), 0.5 * std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * Simulation::Pi<double>() * u2), 1.0e-12);
}


}


namespace PagePmsm {


using Motor = Simulation::Pmsm<double>;


void Run() {
	Motor::Parameters parameters;
	parameters.resistance = 0.5;
	parameters.ld = 2.0e-3;
	parameters.lq = 3.0e-3;
	parameters.psi = 0.01;
	parameters.polePairs = 4;

	Motor motor(parameters);
	motor.SetVoltageCommand(1.0, 0.0);
	CheckEqual(motor.CurrentD(), 0.0);
	CheckEqual(motor.CurrentQ(), 0.0);
	CheckEqual(motor.ThetaE(), 0.0);
	CheckEqual(motor.Torque(), 0.0);
}


}


namespace PageBrushedDc {


void Run() {
	Simulation::BrushedDc<double>::Parameters parameters;
	parameters.resistance = 1.0;
	parameters.inductance = 1.0e-3;
	parameters.torqueConstant = 0.05;

	Simulation::BrushedDc<double> motor(parameters);
	motor.SetVoltageCommand(6.0);
	CheckEqual(motor.Current(), 0.0);
	CheckEqual(motor.Torque(), 0.0);
}


}


namespace PageRotor {


void Run() {
	Simulation::Rotor<double> rotor;
	rotor.SetInertia(1.0e-4);
	rotor.SetViscous(1.0e-6);
	rotor.SetCoulomb(1.0e-5);
	rotor.SetInitialSpeed(10.0);
	rotor.SetInitialAngle(0.5);
	CheckEqual(rotor.Speed(), 0.0);
	CheckEqual(rotor.ThetaM(), 0.0);
}


}


namespace PageTwoMassShaft {


void Run() {
	Simulation::TwoMassShaft<double> shaft;
	shaft.SetMotorInertia(1.0e-4);
	shaft.SetLoadInertia(4.0e-4);
	shaft.SetStiffness(100.0);
	shaft.SetDamping(0.01);
	shaft.SetMotorViscous(1.0e-6);
	shaft.SetLoadViscous(1.0e-6);
	shaft.SetInitialMotorAngle(0.1);
	CheckEqual(shaft.Speed(), 0.0);
	CheckEqual(shaft.ThetaM(), 0.0);
	CheckEqual(shaft.LoadSpeed(), 0.0);
	CheckEqual(shaft.LoadThetaM(), 0.0);
}


}


namespace PageRoadLoad {


void Run() {
	Simulation::RoadLoad<double> road;
	road.SetMass(1000.0);
	road.SetWheelRadius(0.3);
	road.SetGearRatio(10.0);
	road.SetRollingResistance(0.01);
	road.SetDragArea(0.6);
	road.SetAirDensity(1.2);
	road.SetGrade(0.02);
	CheckEqual(road.LoadTorque(), 0.0);
	CheckEqual(road.ReflectedInertia(), 0.0);
}


}


namespace PageLoads {


using Fan = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 2.0e-3, 0.01, 4 })
		.Add<Simulation::Rotor>()
			.Inertia(2.0e-5)
			.Coulomb(1.0e-6)
		.Add<Simulation::QuadraticLoad>()
			.Coefficient(1.0e-6);
}>;


void Run() {
	Simulation::ConstantLoad<double> constant;
	constant.SetTorque(0.01);
	CheckEqual(constant.Torque(), 0.01);

	Simulation::QuadraticLoad<double> fan;
	fan.SetCoefficient(1.0e-6);
	CheckEqual(fan.Coefficient(), 1.0e-6);

	// No command: the fan has no torque to fight, and the rotor stays at rest.
	Fan plant;
	plant.Advance(1.0e-4);
	CheckEqual(plant.Get<Simulation::Rotor>().Speed(), 0.0);
}


}


namespace PageInverter {


void Run() {
	Simulation::Inverter<double> inverter;
	inverter.SetDcBus(14.8);
	CheckEqual(inverter.AppliedD(), 0.0);
	CheckEqual(inverter.AppliedQ(), 0.0);
}


}


namespace PageEncoder {


void Run() {
	Simulation::Encoder<double> encoder;
	encoder.SetCounts(2048);
	CheckEqual(encoder.Counts(), 0u);
}


}


namespace PageMagneticSensor {


void Run() {
	Simulation::MagneticSensor<double> sensor;
	sensor.SetNoiseDeg(0.05);
	sensor.SetSeed(7);
	CheckEqual(sensor.Angle(), 0.0);
}


}


namespace PageCurrentSense {


void Run() {
	Simulation::CurrentSense<double> sense;
	sense.SetOffsetA(0.002);
	sense.SetOffsetB(-0.001);
	sense.SetOffsetC(0.0);
	sense.SetGainError(0.005);
	CheckEqual(sense.PhaseA(), 0.0);
	CheckEqual(sense.PhaseB(), 0.0);
	CheckEqual(sense.PhaseC(), 0.0);
}


}


namespace PageThermal {


using Heater = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.AddThermal<5>()
			.Ambient(25.0)
			.ThermalResistance(5.0)
			.ThermalCapacitance(10.0)
			.DelaySteps(5)
			.Power(10.0)
		.Substeps(10);
}>;


void Run() {
	// The power is delayed by five periods, so the first period has no heat: the temperature is still the ambient.
	Heater heater;
	heater.Advance(0.1);
	CheckEqual(heater.Thermal().Temperature(), 25.0);

	Simulation::FirstOrderThermal<double, 5> block;
	block.SetAmbient(25.0);
	block.SetResistance(5.0);
	block.SetCapacitance(10.0);
	block.SetDelaySteps(5);
	block.SetPower(10.0);
	CheckEqual(block.Temperature(), 0.0);

	using Heating = Simulation::FirstOrderThermal<double, 5>;
	CheckEqual(Heating::maxDelaySteps, static_cast<std::size_t>(5));

	Simulation::DelayLine<double, 8> line;
	line.Push(1.5);
	CheckEqual(line.Delayed(1), 1.5);
}


}


namespace PageMassSpringDamper {


void Run() {
	Simulation::MassSpringDamper<double>::Parameters parameters;
	parameters.mass = 1.0;
	parameters.stiffness = 100.0;
	parameters.damping = 2.0;

	Simulation::MassSpringDamper<double> oscillator(parameters);
	oscillator.SetForce(0.0);
	oscillator.SetInitialPosition(1.0);
	oscillator.SetInitialVelocity(0.0);
	CheckEqual(oscillator.Position(), 0.0);
	CheckEqual(oscillator.Velocity(), 0.0);
}


}


namespace PageCartPendulum {


void Run() {
	Simulation::CartPendulum<double>::Parameters parameters;
	parameters.cartMass = 1.0;
	parameters.poleMass = 0.1;
	parameters.length = 0.5;

	Simulation::CartPendulum<double> cart(parameters);
	cart.SetForce(0.0);
	cart.SetInitialPosition(0.0);
	cart.SetInitialAngle(1.0e-4);
	CheckEqual(cart.CartPosition(), 0.0);
	CheckEqual(cart.PoleAngle(), 0.0);
}


}


int main() {
	PagePlant::Run();
	PagePlantMembers::Run();
	PageInteger::Run();
	PageIntegrator::Run();
	PageNoise::Run();
	PagePmsm::Run();
	PageBrushedDc::Run();
	PageRotor::Run();
	PageTwoMassShaft::Run();
	PageRoadLoad::Run();
	PageLoads::Run();
	PageInverter::Run();
	PageEncoder::Run();
	PageMagneticSensor::Run();
	PageCurrentSense::Run();
	PageThermal::Run();
	PageMassSpringDamper::Run();
	PageCartPendulum::Run();

	return VhalCheck::Finish("Simulation wiki examples");
}
