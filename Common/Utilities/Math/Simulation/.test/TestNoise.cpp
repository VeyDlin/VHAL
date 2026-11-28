#include <VhalCheck.h>
#include <Utilities/Math/Simulation/Noise.h>
#include <cstddef>


// Checks of the seeded generator: the uniform and Gaussian moments over 200000 samples against their analytic values, with the
// tolerance about six standard errors of each moment, and the reproducibility of a seed.
int main() {
	constexpr std::size_t samples = 200000;

	Simulation::Noise uniform(12345);
	double uniformSum = 0.0;
	double uniformSquares = 0.0;

	for (std::size_t i = 0; i < samples; i++) {
		const double value = uniform.Uniform();
		uniformSum += value;
		uniformSquares += value * value;
	}

	const double uniformMean = uniformSum / static_cast<double>(samples);
	const double uniformVariance = uniformSquares / static_cast<double>(samples) - uniformMean * uniformMean;

	// U(0, 1): mean 1/2, variance 1/12. The standard error of the mean is sqrt(1/12 / N) = 6.5e-4, and that of the variance estimate
	// is sqrt((mu4 - sigma^4) / N) = 1.7e-4 with mu4 = 1/80 (both with N = 200000). The tolerances are six of each.
	VHAL_CHECK_NEAR(uniformMean, 0.5, 0.004);
	VHAL_CHECK_NEAR(uniformVariance, 1.0 / 12.0, 0.001);

	Simulation::Noise gaussian(777);
	const double sigma = 0.5;
	double gaussianSum = 0.0;
	double gaussianSquares = 0.0;

	for (std::size_t i = 0; i < samples; i++) {
		const double value = gaussian.Gaussian(sigma);
		gaussianSum += value;
		gaussianSquares += value * value;
	}

	const double gaussianMean = gaussianSum / static_cast<double>(samples);
	const double gaussianVariance = gaussianSquares / static_cast<double>(samples) - gaussianMean * gaussianMean;

	// Mean 0, variance sigma^2: the standard error of the mean is sigma / sqrt(N) = 0.0011, and that of the variance estimate is
	// sigma^2 sqrt(2 / N) = 0.0008. The tolerances are about six of each.
	VHAL_CHECK_NEAR(gaussianMean, 0.0, 0.007);
	VHAL_CHECK_NEAR(gaussianVariance, sigma * sigma, 0.005);

	// The same seed gives the same sequence, and another seed does not.
	Simulation::Noise first(42);
	Simulation::Noise second(42);
	Simulation::Noise other(43);
	bool same = true;
	bool differs = false;

	for (int i = 0; i < 100; i++) {
		const double a = first.Uniform();
		const double b = second.Uniform();
		const double c = other.Uniform();
		same = same && a == b;
		differs = differs || a != c;
	}

	VHAL_CHECK(same);
	VHAL_CHECK(differs);

	return VhalCheck::Finish("Simulation noise");
}
