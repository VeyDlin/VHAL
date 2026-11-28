// Every snippet of the Simulation wiki pages compiles here, and the checks of the snippets that run hold. Each page's snippets have
// their own namespace, because they reuse names. The snippets are the code of the pages, copied as they are.
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
#include <cstddef>
#include <cstdint>


namespace PagePlant {


// The smallest chain: a PMSM with an ideal rotor and an average inverter.
using Smallest = Simulation::Plant<+[] {
	return Simulation::Builder()
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
	return Simulation::Builder()
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
	Smallest plant;
	plant.Get<Simulation::Pmsm>().SetVoltageCommand(0.0, 1.0);
	plant.Advance(100.0e-6);
	const double speed = plant.Get<Simulation::Rotor>().Speed();
	const double current = plant.Get<Simulation::Pmsm>().CurrentQ();
	const double torque = plant.Get<Simulation::Pmsm>().Torque();
	const double angle = plant.Get<Simulation::Rotor>().ThetaM();
	const double applied = plant.Get<Simulation::Inverter>().AppliedQ();
	const std::size_t states = Smallest::stateCount;
	VHAL_CHECK(speed == speed);
	VHAL_CHECK(current == current);
	VHAL_CHECK(torque == torque);
	VHAL_CHECK(angle == angle);
	VHAL_CHECK(applied == applied);
	VHAL_CHECK_EQ(states, static_cast<std::size_t>(5));

	Largest large;
	large.Advance(100.0e-6);
	VHAL_CHECK(large.Get<Simulation::TwoMassShaft>().LoadSpeed() == large.Get<Simulation::TwoMassShaft>().LoadSpeed());
	VHAL_CHECK(large.Road().LoadTorque() == large.Road().LoadTorque());
	VHAL_CHECK(large.EncoderBlock().Counts() < 2048u);
	VHAL_CHECK(large.CurrentSenseBlock().PhaseA() == large.CurrentSenseBlock().PhaseA());
	VHAL_CHECK(large.Get<Simulation::MagneticSensor>().Angle() == large.Get<Simulation::MagneticSensor>().Angle());
}


}


namespace PageInteger {


void Run() {
	const double value = Simulation::WrapAngle(-0.5);
	VHAL_CHECK(value > 0.0);
	VHAL_CHECK(Simulation::Sign(-3.0) == -1.0);
	VHAL_CHECK(Simulation::Sign(0.0) == 0.0);
	VHAL_CHECK(Simulation::TwoPi<double>() > 6.28);
	VHAL_CHECK(Simulation::StandardGravity<double>() > 9.8);
}


}


namespace PageIntegrator {


struct Decay {
	void operator()(const double* state, double* derivative) const {
		derivative[0] = -state[0];
	}
};


void Run() {
	std::array<double, 1> state{ 1.0 };
	Decay decay;
	Simulation::Integrator<double, 1>::Step(state, 0.1, decay);
	VHAL_CHECK(state[0] < 1.0);
}


}


namespace PageNoise {


void Run() {
	Simulation::Noise noise(1234);
	const double uniform = noise.Uniform();
	const double gaussian = noise.Gaussian(0.5);
	VHAL_CHECK(uniform > 0.0 && uniform < 1.0);
	VHAL_CHECK(gaussian == gaussian);
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
	VHAL_CHECK(motor.CurrentD() == 0.0);
	VHAL_CHECK(motor.CurrentQ() == 0.0);
	VHAL_CHECK(motor.ThetaE() == 0.0);
	VHAL_CHECK(motor.Torque() == 0.0);
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
	VHAL_CHECK(motor.Current() == 0.0);
	VHAL_CHECK(motor.Torque() == 0.0);
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
	VHAL_CHECK(rotor.Speed() == 0.0);
	VHAL_CHECK(rotor.ThetaM() == 0.0);
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
	VHAL_CHECK(shaft.Speed() == 0.0);
	VHAL_CHECK(shaft.ThetaM() == 0.0);
	VHAL_CHECK(shaft.LoadSpeed() == 0.0);
	VHAL_CHECK(shaft.LoadThetaM() == 0.0);
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
	VHAL_CHECK(road.LoadTorque() == 0.0);
	VHAL_CHECK(road.ReflectedInertia() == 0.0);
}


}


namespace PageLoads {


void Run() {
	Simulation::ConstantLoad<double> constant;
	constant.SetTorque(0.01);
	VHAL_CHECK(constant.Torque() == 0.01);

	Simulation::QuadraticLoad<double> fan;
	fan.SetCoefficient(1.0e-6);
	VHAL_CHECK(fan.Coefficient() == 1.0e-6);
}


}


namespace PageInverter {


void Run() {
	Simulation::Inverter<double> inverter;
	inverter.SetDcBus(14.8);
	VHAL_CHECK(inverter.AppliedD() == 0.0);
	VHAL_CHECK(inverter.AppliedQ() == 0.0);
}


}


namespace PageEncoder {


void Run() {
	Simulation::Encoder<double> encoder;
	encoder.SetCounts(2048);
	VHAL_CHECK(encoder.Counts() == 0u);
}


}


namespace PageMagneticSensor {


void Run() {
	Simulation::MagneticSensor<double> sensor;
	sensor.SetNoiseDeg(0.05);
	sensor.SetSeed(7);
	VHAL_CHECK(sensor.Angle() == 0.0);
}


}


namespace PageCurrentSense {


void Run() {
	Simulation::CurrentSense<double> sense;
	sense.SetOffsetA(0.002);
	sense.SetOffsetB(-0.001);
	sense.SetOffsetC(0.0);
	sense.SetGainError(0.005);
	VHAL_CHECK(sense.PhaseA() == 0.0);
	VHAL_CHECK(sense.PhaseB() == 0.0);
	VHAL_CHECK(sense.PhaseC() == 0.0);
}


}


namespace PageThermal {


using Heater = Simulation::Plant<+[] {
	return Simulation::Builder()
		.Add<Simulation::FirstOrderThermal>()
			.Ambient(25.0)
			.ThermalResistance(5.0)
			.ThermalCapacitance(10.0)
			.DelaySteps(5)
			.Power(10.0)
		.Substeps(10);
}>;


void Run() {
	Heater heater;
	heater.Advance(0.1);
	VHAL_CHECK(heater.Get<Simulation::FirstOrderThermal>().Temperature() == 25.0);

	Simulation::DelayLine<double, 8> line;
	line.Push(1.5);
	VHAL_CHECK(line.Delayed(1) == 1.5);
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
	VHAL_CHECK(oscillator.Position() == 0.0);
	VHAL_CHECK(oscillator.Velocity() == 0.0);
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
	VHAL_CHECK(cart.CartPosition() == 0.0);
	VHAL_CHECK(cart.PoleAngle() == 0.0);
}


}


int main() {
	PagePlant::Run();
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
