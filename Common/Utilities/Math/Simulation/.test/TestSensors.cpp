#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Motors/Pmsm.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Plant.h>
#include <Utilities/Math/Simulation/Sensors/CurrentSense.h>
#include <Utilities/Math/Simulation/Sensors/Encoder.h>
#include <Utilities/Math/Simulation/Sensors/MagneticSensor.h>
#include <Utilities/Math/Simulation/Angles.h>
#include <cmath>
#include <cstddef>
#include <cstdint>


// Checks of the sensors: the encoder counts against floor(theta / 2 pi * Counts), the phase-current sense against the inverse
// transforms written out, and the magnetic sensor's error against its Gaussian standard deviation over 20000 periods.
namespace {


struct EncoderBus : Simulation::Encoder<double>::Signals {
	double thetaM = 0.0;
};


struct SenseBus : Simulation::CurrentSense<double>::Signals, Simulation::Pmsm<double>::Signals {
};


using Machine = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 2.0e-3, 0.01, 4 })
		.Add<Simulation::Rotor>()
			.Inertia(1.0e-3)
		.Add<Simulation::MagneticSensor>()
			.NoiseDeg(0.5)
			.Seed(99);
}>;


void CheckEncoder() {
	Simulation::Encoder<double> encoder;
	encoder.SetCounts(2048);
	const double dummy[1] = { 0.0 };

	const std::uint32_t expected[4] = { 0, 1, 1000, 2047 };
	const int samples[4] = { 0, 1, 1000, 2047 };

	for (int k = 0; k < 4; k++) {
		EncoderBus bus;
		bus.thetaM = (static_cast<double>(samples[k]) + 0.5) * 2.0 * 3.141592653589793 / 2048.0;
		encoder.Publish(dummy, bus);
		encoder.Latch(bus);
		VHAL_CHECK_EQ(encoder.Counts(), expected[k]);
	}

	// A negative angle is the last count of the turn before it, and a turn more is the same count.
	EncoderBus negative;
	negative.thetaM = -0.5 * 2.0 * 3.141592653589793 / 2048.0;
	encoder.Publish(dummy, negative);
	encoder.Latch(negative);
	VHAL_CHECK_EQ(encoder.Counts(), static_cast<std::uint32_t>(2047));

	EncoderBus turns;
	turns.thetaM = 3.0 * 2.0 * 3.141592653589793 + 1.0 * 2.0 * 3.141592653589793 / 2048.0 + 0.1 * 2.0 * 3.141592653589793 / 2048.0;
	encoder.Publish(dummy, turns);
	encoder.Latch(turns);
	VHAL_CHECK_EQ(encoder.Counts(), static_cast<std::uint32_t>(1));
}


void CheckCurrentSense() {
	Simulation::CurrentSense<double> sense;
	sense.SetOffsetA(0.1);
	sense.SetOffsetB(-0.05);
	sense.SetOffsetC(0.0);
	sense.SetGainError(0.02);

	const double dummy[1] = { 0.0 };
	const double d = 3.0;
	const double q = 4.0;
	const double theta = 0.3;

	SenseBus bus;
	bus.currentD = d;
	bus.currentQ = q;
	bus.thetaE = theta;
	sense.Publish(dummy, bus);
	sense.Latch(bus);

	// The true phase currents, from the inverse Park and the inverse Clarke transforms written out.
	const double alpha = d * std::cos(theta) - q * std::sin(theta);
	const double beta = d * std::sin(theta) + q * std::cos(theta);
	const double a = alpha;
	const double b = 0.5 * std::sqrt(3.0) * beta - 0.5 * alpha;
	const double c = -0.5 * alpha - 0.5 * std::sqrt(3.0) * beta;

	VHAL_CHECK_NEAR(a + b + c, 0.0, 1.0e-12);
	VHAL_CHECK_NEAR(sense.PhaseA(), 1.02 * a + 0.1, 1.0e-9);
	VHAL_CHECK_NEAR(sense.PhaseB(), 1.02 * b - 0.05, 1.0e-9);
	VHAL_CHECK_NEAR(sense.PhaseC(), 1.02 * c, 1.0e-9);
}


void CheckMagneticNoise() {
	Machine machine;
	const std::size_t periods = 20000;
	double sum = 0.0;
	double squares = 0.0;

	for (std::size_t i = 0; i < periods; i++) {
		machine.Advance(1.0e-4);
		double error = machine.Get<Simulation::MagneticSensor>().Angle();

		if (error > Simulation::Pi<double>()) {
			error -= Simulation::TwoPi<double>();
		}

		sum += error;
		squares += error * error;
	}

	const double mean = sum / static_cast<double>(periods);
	const double variance = squares / static_cast<double>(periods) - mean * mean;
	const double sigma = 0.5 * Simulation::Pi<double>() / 180.0;

	// The standard errors are sigma / sqrt(N) = 6e-5 for the mean and 0.5 percent of sigma^2 for the variance.
	VHAL_CHECK_NEAR(mean, 0.0, 4.0e-4);
	VHAL_CHECK_NEAR(variance, sigma * sigma, 0.03 * sigma * sigma);
}


}


int main() {
	CheckEncoder();
	CheckCurrentSense();
	CheckMagneticNoise();

	return VhalCheck::Finish("Simulation sensors");
}
