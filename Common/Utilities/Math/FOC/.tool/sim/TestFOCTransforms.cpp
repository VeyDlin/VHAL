#include "FOCCheck.h"
#include <Utilities/Math/FOC/ClarkeTransformation.h>
#include <Utilities/Math/FOC/ParkTransformation.h>
#include <Utilities/Math/FOC/VoltageCalculator.h>
#include <Utilities/Math/FOC/SpaceVectorGenerator.h>
#include <Utilities/Math/FOC/SVGenMF.h>
#include <Utilities/Math/FOC/RampGenerator.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdio>


// Host checks for the stateless transforms (Clarke, Park, VoltageCalculator) against their formulas in double, and for
// the generators (SpaceVectorGenerator, SVGenMF, RampGenerator) against the same template at double. Each input is
// rounded to T first, so the error is the arithmetic of the class alone. The double instance is a plain evaluation of
// the same equations.


namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double sqrt3 = 1.73205080756887729353;
constexpr int sweepSteps = 720;
constexpr int generatorSteps = 3000;


template<typename T>
void ClarkeChecks(const char* name, double bound) {
	double maxC2 = 0;
	double maxC3 = 0;
	double maxInverse = 0;

	for (int k = 0; k < sweepSteps; k++) {
		double angle = 2 * pi * k / sweepSteps;
		double a = FOCCheck::Quantized<T>(1.2 * std::cos(angle));
		double b = FOCCheck::Quantized<T>(0.9 * std::cos(angle - 2 * pi / 3));
		double c = FOCCheck::Quantized<T>(-0.7 * std::sin(angle));

		ClarkeTransformation<T> clarke;
		typename ClarkeTransformation<T>::Phase phase{T(a), T(b), T(c)};

		clarke.SetPhase(phase).Resolve(ClarkeTransformation<T>::CurrentsMode::C2);
		double betaC2 = (a + 2 * b) / sqrt3;
		maxC2 = std::fmax(maxC2, std::fabs(FOCCheck::ToDouble(clarke.GetClarke().beta) - betaC2));
		maxC2 = std::fmax(maxC2, std::fabs(FOCCheck::ToDouble(clarke.GetClarke().alpha) - a));

		clarke.SetPhase(phase).Resolve(ClarkeTransformation<T>::CurrentsMode::C3);
		double betaC3 = (b - c) / sqrt3;
		maxC3 = std::fmax(maxC3, std::fabs(FOCCheck::ToDouble(clarke.GetClarke().beta) - betaC3));

		typename ClarkeTransformation<T>::Clarke clarkeIn{T(a), T(b)};
		clarke.SetClarke(clarkeIn).ResolveInverse();
		double phaseB = (sqrt3 / 2) * b - 0.5 * a;
		double phaseC = -0.5 * a - (sqrt3 / 2) * b;
		maxInverse = std::fmax(maxInverse, std::fabs(FOCCheck::ToDouble(clarke.GetPhase().b) - phaseB));
		maxInverse = std::fmax(maxInverse, std::fabs(FOCCheck::ToDouble(clarke.GetPhase().c) - phaseC));
	}

	char label[96];
	std::snprintf(label, sizeof label, "Clarke C2 %s", name);
	FOCCheck::Report(label, maxC2, bound, __LINE__);
	std::snprintf(label, sizeof label, "Clarke C3 %s", name);
	FOCCheck::Report(label, maxC3, bound, __LINE__);
	std::snprintf(label, sizeof label, "Clarke inverse %s", name);
	FOCCheck::Report(label, maxInverse, bound, __LINE__);
}


template<typename T>
void ParkChecks(const char* name, double bound) {
	double maxForward = 0;
	double maxInverse = 0;

	for (int k = 0; k < sweepSteps; k++) {
		double angle = FOCCheck::Quantized<T>(2 * pi * k / sweepSteps);
		double alpha = FOCCheck::Quantized<T>(1.1 * std::cos(3 * angle) - 0.2);
		double beta = FOCCheck::Quantized<T>(0.9 * std::sin(3 * angle));

		ParkTransformation<T> park;
		typename ParkTransformation<T>::Axis axis{T(alpha), T(beta), T(angle)};
		park.SetAxis(axis).Resolve();

		double d = alpha * std::cos(angle) + beta * std::sin(angle);
		double q = beta * std::cos(angle) - alpha * std::sin(angle);
		maxForward = std::fmax(maxForward, std::fabs(FOCCheck::ToDouble(park.GetPark().dAxis) - d));
		maxForward = std::fmax(maxForward, std::fabs(FOCCheck::ToDouble(park.GetPark().qAxis) - q));

		typename ParkTransformation<T>::Park pk{T(d), T(q), T(angle)};
		park.SetPark(pk).ResolveInverse();
		maxInverse = std::fmax(maxInverse, std::fabs(FOCCheck::ToDouble(park.GetAxis().alpha) - (d * std::cos(angle) - q * std::sin(angle))));
		maxInverse = std::fmax(maxInverse, std::fabs(FOCCheck::ToDouble(park.GetAxis().beta) - (d * std::sin(angle) + q * std::cos(angle))));
	}

	char label[96];
	std::snprintf(label, sizeof label, "Park forward %s", name);
	FOCCheck::Report(label, maxForward, bound, __LINE__);
	std::snprintf(label, sizeof label, "Park inverse %s", name);
	FOCCheck::Report(label, maxInverse, bound, __LINE__);
}


template<typename T>
void VoltageChecks(const char* name, double bound) {
	double maxError = 0;

	for (int k = 0; k < generatorSteps; k++) {
		double angle = 2 * pi * 50.0 * k * 1e-4;
		double dc = FOCCheck::Quantized<T>(1.0);
		double ma = FOCCheck::Quantized<T>(0.5 + 0.4 * std::sin(angle));
		double mb = FOCCheck::Quantized<T>(0.5 + 0.4 * std::sin(angle - 2 * pi / 3));
		double mc = FOCCheck::Quantized<T>(0.5 + 0.4 * std::sin(angle + 2 * pi / 3));

		VoltageCalculator<T> calc;
		calc.Set(T(dc), T(ma), T(mb), T(mc)).Resolve();

		// Phase voltages as the class forms them: one third of the bus voltage times the modulation difference.
		double phaseA = dc / 3 * (2 * ma - mb - mc);
		double phaseB = dc / 3 * (2 * mb - ma - mc);
		double phaseC = dc / 3 * (2 * mc - mb - ma);
		double alpha = phaseA;
		double beta = (phaseA + 2 * phaseB) / sqrt3;

		maxError = std::fmax(maxError, std::fabs(FOCCheck::ToDouble(calc.Get().phaseA) - phaseA));
		maxError = std::fmax(maxError, std::fabs(FOCCheck::ToDouble(calc.Get().phaseB) - phaseB));
		maxError = std::fmax(maxError, std::fabs(FOCCheck::ToDouble(calc.Get().phaseC) - phaseC));
		maxError = std::fmax(maxError, std::fabs(FOCCheck::ToDouble(calc.Get().alpha) - alpha));
		maxError = std::fmax(maxError, std::fabs(FOCCheck::ToDouble(calc.Get().beta) - beta));
	}

	char label[96];
	std::snprintf(label, sizeof label, "VoltageCalculator %s", name);
	FOCCheck::Report(label, maxError, bound, __LINE__);
}


template<typename T>
void SpaceVectorChecks(const char* name, double bound) {
	SpaceVectorGenerator<double> referenceSvg;
	SpaceVectorGenerator<T> svg;
	double maxSvpwm = 0;
	double maxCommon = 0;
	double maxDiscontinuous = 0;

	for (int k = 0; k < generatorSteps; k++) {
		double angle = 2 * pi * 0.01 * k;
		double alpha = FOCCheck::Quantized<T>(0.8 * std::cos(angle));
		double beta = FOCCheck::Quantized<T>(0.8 * std::sin(angle));

		svg.Set(T(alpha), T(beta)).Resolve();
		referenceSvg.Set(alpha, beta).Resolve();
		maxSvpwm = std::fmax(maxSvpwm, std::fabs(FOCCheck::ToDouble(svg.Get().phaseA) - referenceSvg.Get().phaseA));
		maxSvpwm = std::fmax(maxSvpwm, std::fabs(FOCCheck::ToDouble(svg.Get().phaseB) - referenceSvg.Get().phaseB));
		maxSvpwm = std::fmax(maxSvpwm, std::fabs(FOCCheck::ToDouble(svg.Get().phaseC) - referenceSvg.Get().phaseC));

		svg.ResolveCommonMode();
		referenceSvg.ResolveCommonMode();
		maxCommon = std::fmax(maxCommon, std::fabs(FOCCheck::ToDouble(svg.Get().phaseA) - referenceSvg.Get().phaseA));
		maxCommon = std::fmax(maxCommon, std::fabs(FOCCheck::ToDouble(svg.Get().phaseB) - referenceSvg.Get().phaseB));

		svg.ResolveDiscontinuousPwmMode();
		referenceSvg.ResolveDiscontinuousPwmMode();
		maxDiscontinuous = std::fmax(maxDiscontinuous, std::fabs(FOCCheck::ToDouble(svg.Get().phaseA) - referenceSvg.Get().phaseA));
		maxDiscontinuous = std::fmax(maxDiscontinuous, std::fabs(FOCCheck::ToDouble(svg.Get().phaseC) - referenceSvg.Get().phaseC));
	}

	char label[96];
	std::snprintf(label, sizeof label, "SpaceVectorGenerator SVPWM %s", name);
	FOCCheck::Report(label, maxSvpwm, bound, __LINE__);
	std::snprintf(label, sizeof label, "SpaceVectorGenerator common mode %s", name);
	FOCCheck::Report(label, maxCommon, bound, __LINE__);
	std::snprintf(label, sizeof label, "SpaceVectorGenerator discontinuous %s", name);
	FOCCheck::Report(label, maxDiscontinuous, bound, __LINE__);
}


template<typename T>
void SVGenMFChecks(const char* name, double bound) {
	SVGenMF<double> referenceSvm;
	SVGenMF<T> svm;
	referenceSvm.freqMax = FOCCheck::Quantized<T>(6.0 * 50.0 * 1e-4);
	svm.freqMax = T(6.0 * 50.0 * 1e-4);
	double maxError = 0;

	for (int k = 0; k < generatorSteps; k++) {
		double frequency = FOCCheck::Quantized<T>(0.8);
		double gain = FOCCheck::Quantized<T>(0.9);
		double offset = FOCCheck::Quantized<T>(0.05);

		svm.Set(T(frequency), T(gain), T(offset)).Resolve();
		referenceSvm.Set(frequency, gain, offset).Resolve();
		maxError = std::fmax(maxError, std::fabs(FOCCheck::ToDouble(svm.Get().phaseA) - referenceSvm.Get().phaseA));
		maxError = std::fmax(maxError, std::fabs(FOCCheck::ToDouble(svm.Get().phaseB) - referenceSvm.Get().phaseB));
		maxError = std::fmax(maxError, std::fabs(FOCCheck::ToDouble(svm.Get().phaseC) - referenceSvm.Get().phaseC));
	}

	char label[96];
	std::snprintf(label, sizeof label, "SVGenMF %s", name);
	FOCCheck::Report(label, maxError, bound, __LINE__);
}


template<typename T>
void RampGeneratorChecks(const char* name, double bound, double step) {
	RampGenerator<double> referenceRamp;
	RampGenerator<T> ramp;
	referenceRamp.stepAngleMax = FOCCheck::Quantized<T>(step);
	ramp.stepAngleMax = T(step);
	referenceRamp.outputMax = 1;
	referenceRamp.outputMin = -1;
	ramp.outputMax = T(1);
	ramp.outputMin = T(-1);
	referenceRamp.Set(FOCCheck::Quantized<T>(0.5), 1, 0);
	ramp.Set(T(0.5), T(1), T(0));
	double maxError = 0;

	for (int k = 0; k < generatorSteps; k++) {
		ramp.Resolve();
		referenceRamp.Resolve();
		maxError = std::fmax(maxError, std::fabs(FOCCheck::ToDouble(ramp.Get()) - referenceRamp.Get()));
	}

	char label[96];
	std::snprintf(label, sizeof label, "RampGenerator %s", name);
	FOCCheck::Report(label, maxError, bound, __LINE__);
}

}


int main() {
	// Stateless transforms against their formulas
	ClarkeChecks<float>("float", 1e-6);
	ClarkeChecks<IQ<16>>("IQ16", 1e-4);
	ClarkeChecks<IQ<20>>("IQ20", 5e-6);
	ClarkeChecks<IQ<24>>("IQ24", 5e-7);
	ParkChecks<float>("float", 1e-6);
	ParkChecks<IQ<16>>("IQ16", 1e-4);
	ParkChecks<IQ<20>>("IQ20", 5e-6);
	ParkChecks<IQ<24>>("IQ24", 5e-7);
	VoltageChecks<float>("float", 1e-6);
	VoltageChecks<IQ<16>>("IQ16", 1e-4);
	VoltageChecks<IQ<20>>("IQ20", 5e-6);
	VoltageChecks<IQ<24>>("IQ24", 5e-7);

	// Generators against the double instance of the same template
	SpaceVectorChecks<float>("float", 1e-6);
	SpaceVectorChecks<IQ<16>>("IQ16", 1e-4);
	SpaceVectorChecks<IQ<20>>("IQ20", 5e-6);
	SpaceVectorChecks<IQ<24>>("IQ24", 5e-7);
	SVGenMFChecks<float>("float", 1e-4);
	SVGenMFChecks<IQ<20>>("IQ20", 2.5e-3);
	SVGenMFChecks<IQ<24>>("IQ24", 1.5e-4);
	RampGeneratorChecks<float>("float", 1e-5, 0.01);
	RampGeneratorChecks<IQ<16>>("IQ16", 1e-4, 0.0078125);
	RampGeneratorChecks<IQ<20>>("IQ20", 1e-4, 0.01);
	RampGeneratorChecks<IQ<24>>("IQ24", 1e-4, 0.01);

	return FOCCheck::Finish();
}
