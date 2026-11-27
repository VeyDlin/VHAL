#include "FOCCheck.h"
#include <Utilities/Math/FOC/CurrentModel.h>
#include <Utilities/Math/FOC/ACIFluxEstimator.h>
#include <Utilities/Math/FOC/SlidingModeObserver.h>
#include <Utilities/Math/FOC/ACISpeedEstimator.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>


// Host checks for the estimators and their constants. Each run feeds the same quantized inputs to T and to the double
// instance of the same template, and the error is the difference of the two outputs. The constants come from
// ComputeConstants, which works in SetupReal<T>. The motor values of the reference are the same values in SetupReal<T>, so
// both sides see the same setup inputs, and the double instance holds the constants of those values in double.
// Angles are compared around the circle, in turns.


namespace {

constexpr double pi = 3.14159265358979323846;
constexpr int steps = 3000;
constexpr double ts = 1e-4;
// The settled window of the physical checks: 1200 to 2800, which is a whole number of turns for the rotations used there.
constexpr int settleStart = 1200;
constexpr int settleEnd = 2800;


// A motor parameter set rounded to T, then read back as double for the double instance.
template<typename T>
double Q(double value) {
	return FOCCheck::Quantized<T>(value);
}


// One LSB of T at 1 (the float epsilon for float).
template<typename T>
double Lsb() {
	if constexpr (std::is_floating_point_v<T>) {
		return static_cast<double>(std::numeric_limits<T>::epsilon());
	} else {
		return 1.0 / static_cast<double>(int64(1) << T::q);
	}
}


// The rounding bound of a constant converted once from double: half an LSB for IQ (the conversion rounds to nearest), and
// one float epsilon relative to the value for float.
template<typename T>
double ConstantTolerance(double value) {
	if constexpr (std::is_floating_point_v<T>) {
		return std::fabs(value) * static_cast<double>(std::numeric_limits<T>::epsilon());
	} else {
		return 0.5 / static_cast<double>(int64(1) << T::q);
	}
}


template<typename T>
void CurrentModelChecks(const char* name, double bound) {
	typename CurrentModel<T>::MotorParameters motor{
		.Rr = SetupReal<T>(1.5),
		.Lr = SetupReal<T>(0.2),
		.fb = SetupReal<T>(50.0),
		.Ts = SetupReal<T>(ts)
	};
	typename CurrentModel<double>::MotorParameters motorRef{
		.Rr = Q<SetupReal<T>>(1.5),
		.Lr = Q<SetupReal<T>>(0.2),
		.fb = Q<SetupReal<T>>(50.0),
		.Ts = Q<SetupReal<T>>(ts)
	};

	typename CurrentModel<T>::Constants constants = CurrentModel<T>::ComputeConstants(motor);
	typename CurrentModel<double>::Constants constantsRef = CurrentModel<double>::ComputeConstants(motorRef);
	CurrentModel<T> model;
	CurrentModel<double> reference;
	model.constants = constants;
	reference.constants = constantsRef;

	double maxTheta = 0;
	for (int k = 0; k < steps; k++) {
		double t = k * ts;
		double ids = Q<T>(0.6 + 0.3 * std::sin(2 * pi * 2 * t));
		double iqs = Q<T>(0.8 * std::cos(2 * pi * 2 * t));
		double wr = Q<T>(0.2);

		model.Set(FOCCheck::FromDouble<T>(ids), FOCCheck::FromDouble<T>(iqs), FOCCheck::FromDouble<T>(wr)).Resolve();
		reference.Set(ids, iqs, wr).Resolve();
		maxTheta = std::fmax(maxTheta, FOCCheck::CircularError(FOCCheck::ToDouble(model.Get().theta), reference.Get().theta));
	}

	char label[96];
	std::snprintf(label, sizeof label, "CurrentModel theta %s", name);
	FOCCheck::Report(label, maxTheta, bound, __LINE__);
	// Each constant is checked against its own rounding bound, not the theta bound.
	std::snprintf(label, sizeof label, "CurrentModel Kr %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(constants.Kr) - constantsRef.Kr), ConstantTolerance<T>(constantsRef.Kr), __LINE__);
	std::snprintf(label, sizeof label, "CurrentModel Kt %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(constants.Kt) - constantsRef.Kt), ConstantTolerance<T>(constantsRef.Kt), __LINE__);
	std::snprintf(label, sizeof label, "CurrentModel K %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(constants.K) - constantsRef.K), ConstantTolerance<T>(constantsRef.K), __LINE__);
}


// Start-up with no d-axis current: the magnetizing current is zero until ids becomes non-zero, so the slip (Kt iqs / im) has
// no defined value there. The output must stay finite, and after ids rises it must follow the double instance.
template<typename T>
void CurrentModelStartChecks(const char* name, double bound) {
	typename CurrentModel<T>::MotorParameters motor{
		.Rr = SetupReal<T>(1.5),
		.Lr = SetupReal<T>(0.2),
		.fb = SetupReal<T>(50.0),
		.Ts = SetupReal<T>(ts)
	};
	typename CurrentModel<double>::MotorParameters motorRef{
		.Rr = Q<SetupReal<T>>(1.5),
		.Lr = Q<SetupReal<T>>(0.2),
		.fb = Q<SetupReal<T>>(50.0),
		.Ts = Q<SetupReal<T>>(ts)
	};

	CurrentModel<T> model;
	CurrentModel<double> reference;
	model.constants = CurrentModel<T>::ComputeConstants(motor);
	reference.constants = CurrentModel<double>::ComputeConstants(motorRef);

	bool allFinite = true;
	double maxTheta = 0;
	for (int k = 0; k < 2020; k++) {
		double ids = k < 20 ? 0.0 : Q<T>(0.6);
		double iqs = Q<T>(0.5);
		double wr = Q<T>(0.2);

		model.Set(FOCCheck::FromDouble<T>(ids), FOCCheck::FromDouble<T>(iqs), FOCCheck::FromDouble<T>(wr)).Resolve();
		reference.Set(ids, iqs, wr).Resolve();
		double theta = FOCCheck::ToDouble(model.Get().theta);
		allFinite = allFinite && std::isfinite(theta);
		// A non-finite value on either side is a failure, not a NaN that fmax would ignore.
		double error = FOCCheck::CircularError(theta, reference.Get().theta);
		if (!std::isfinite(error)) {
			error = std::numeric_limits<double>::infinity();
		}
		maxTheta = std::fmax(maxTheta, error);
	}

	char label[96];
	std::snprintf(label, sizeof label, "CurrentModel theta finite from zero ids %s", name);
	FOCCheck::Expect(allFinite, label, __LINE__);
	std::snprintf(label, sizeof label, "CurrentModel theta from zero ids %s", name);
	FOCCheck::Report(label, maxTheta, bound, __LINE__);
}


// The scenario uses the per-unit base values Ib = 4 / 311 and Vb = 1. The physical values are checked in ACIFluxPhysicalChecks.
template<typename T>
void ACIFluxChecks(const char* name, double bound) {
	typename ACIFluxEstimator<T>::MotorParameters motor{
		.Rs = SetupReal<T>(2.0),
		.Rr = SetupReal<T>(1.5),
		.Ls = SetupReal<T>(0.2),
		.Lr = SetupReal<T>(0.2),
		.Lm = SetupReal<T>(0.19),
		.Ib = SetupReal<T>(4.0 / 311.0),
		.Vb = SetupReal<T>(1.0),
		.Ts = SetupReal<T>(ts)
	};
	typename ACIFluxEstimator<double>::MotorParameters motorRef{
		.Rs = Q<SetupReal<T>>(2.0),
		.Rr = Q<SetupReal<T>>(1.5),
		.Ls = Q<SetupReal<T>>(0.2),
		.Lr = Q<SetupReal<T>>(0.2),
		.Lm = Q<SetupReal<T>>(0.19),
		.Ib = Q<SetupReal<T>>(4.0 / 311.0),
		.Vb = Q<SetupReal<T>>(1.0),
		.Ts = Q<SetupReal<T>>(ts)
	};

	ACIFluxEstimator<T> estimator;
	ACIFluxEstimator<double> reference;
	estimator.constants = ACIFluxEstimator<T>::ComputeConstants(motor);
	reference.constants = ACIFluxEstimator<double>::ComputeConstants(motorRef);
	estimator.piGains = {.Kp = T(0.5), .Ki = T(0.02)};
	reference.piGains = {.Kp = Q<T>(0.5), .Ki = Q<T>(0.02)};

	double maxTheta = 0;
	double maxPsiD = 0;
	double maxPsiQ = 0;
	for (int k = 0; k < steps; k++) {
		double w = 2 * pi * 50 * k * ts;
		double ids = Q<T>(0.5 * std::cos(w));
		double iqs = Q<T>(0.8 * std::sin(w));
		double uds = Q<T>(0.7 * std::cos(w));
		double uqs = Q<T>(0.7 * std::sin(w));

		estimator.Set(FOCCheck::FromDouble<T>(ids), FOCCheck::FromDouble<T>(iqs), FOCCheck::FromDouble<T>(uds), FOCCheck::FromDouble<T>(uqs)).Resolve();
		reference.Set(ids, iqs, uds, uqs).Resolve();
		maxTheta = std::fmax(maxTheta, FOCCheck::CircularError(FOCCheck::ToDouble(estimator.Get().thetaFlux), reference.Get().thetaFlux));
		maxPsiD = std::fmax(maxPsiD, std::fabs(FOCCheck::ToDouble(estimator.Get().psiDrS) - reference.Get().psiDrS));
		maxPsiQ = std::fmax(maxPsiQ, std::fabs(FOCCheck::ToDouble(estimator.Get().psiQrS) - reference.Get().psiQrS));
	}

	char label[96];
	std::snprintf(label, sizeof label, "ACIFluxEstimator theta %s", name);
	FOCCheck::Report(label, maxTheta, bound, __LINE__);
	std::snprintf(label, sizeof label, "ACIFluxEstimator psiDr %s", name);
	FOCCheck::Report(label, maxPsiD, bound, __LINE__);
	std::snprintf(label, sizeof label, "ACIFluxEstimator psiQr %s", name);
	FOCCheck::Report(label, maxPsiQ, bound, __LINE__);
}


template<typename T>
void SlidingModeChecks(const char* name, double bound) {
	typename SlidingModeObserver<T>::MotorParameters motor{
		.Rs = SetupReal<T>(2.0),
		.Ls = SetupReal<T>(0.2),
		.Ib = SetupReal<T>(4.0 / 311.0),
		.Vb = SetupReal<T>(1.0),
		.Ts = SetupReal<T>(ts)
	};
	typename SlidingModeObserver<double>::MotorParameters motorRef{
		.Rs = Q<SetupReal<T>>(2.0),
		.Ls = Q<SetupReal<T>>(0.2),
		.Ib = Q<SetupReal<T>>(4.0 / 311.0),
		.Vb = Q<SetupReal<T>>(1.0),
		.Ts = Q<SetupReal<T>>(ts)
	};

	SlidingModeObserver<T> observer;
	SlidingModeObserver<double> reference;
	typename SlidingModeObserver<T>::Constants constants = SlidingModeObserver<T>::ComputeConstants(motor);
	typename SlidingModeObserver<double>::Constants constantsRef = SlidingModeObserver<double>::ComputeConstants(motorRef);
	observer.constants = constants;
	reference.constants = constantsRef;
	observer.Kslide = T(0.5);
	observer.Kslf = T(0.1);
	reference.Kslide = Q<T>(0.5);
	reference.Kslf = Q<T>(0.1);

	double maxTheta = 0;
	for (int k = 0; k < steps; k++) {
		double w = 2 * pi * 50 * k * ts;
		double vAlpha = Q<T>(0.6 * std::cos(w));
		double vBeta = Q<T>(0.6 * std::sin(w));
		double iAlpha = Q<T>(0.8 * std::cos(w));
		double iBeta = Q<T>(0.8 * std::sin(w));

		observer.Set(FOCCheck::FromDouble<T>(vAlpha), FOCCheck::FromDouble<T>(vBeta), FOCCheck::FromDouble<T>(iAlpha), FOCCheck::FromDouble<T>(iBeta)).Resolve();
		reference.Set(vAlpha, vBeta, iAlpha, iBeta).Resolve();
		maxTheta = std::fmax(maxTheta, FOCCheck::CircularError(FOCCheck::ToDouble(observer.Get().theta), reference.Get().theta));
	}

	char label[96];
	std::snprintf(label, sizeof label, "SlidingModeObserver theta %s", name);
	FOCCheck::Report(label, maxTheta, bound, __LINE__);
	std::snprintf(label, sizeof label, "SlidingModeObserver Fsmopos %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(constants.Fsmopos) - constantsRef.Fsmopos), ConstantTolerance<T>(constantsRef.Fsmopos), __LINE__);
	std::snprintf(label, sizeof label, "SlidingModeObserver Gsmopos %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(constants.Gsmopos) - constantsRef.Gsmopos), ConstantTolerance<T>(constantsRef.Gsmopos), __LINE__);
}


// The flux angle rotates at rateHz, so the speed is rateHz / fb in pu, and K2 = 1 / (fb Ts). The start offset keeps the
// angle off the window edges 0.2 and 0.8: on an edge, float and double can take different branches (float(0.2) is above
// 0.2), which is a real difference of the comparison and not of the arithmetic.
template<typename T>
void ACISpeedChecks(const char* name, double bound, double baseFrequency, double rateHz) {
	typename ACISpeedEstimator<T>::MotorParameters motor{
		.Rr = SetupReal<T>(1.5),
		.Lr = SetupReal<T>(0.2),
		.fb = SetupReal<T>(baseFrequency),
		.fc = SetupReal<T>(20.0),
		.Ts = SetupReal<T>(ts)
	};
	typename ACISpeedEstimator<double>::MotorParameters motorRef{
		.Rr = Q<SetupReal<T>>(1.5),
		.Lr = Q<SetupReal<T>>(0.2),
		.fb = Q<SetupReal<T>>(baseFrequency),
		.fc = Q<SetupReal<T>>(20.0),
		.Ts = Q<SetupReal<T>>(ts)
	};

	ACISpeedEstimator<T> estimator;
	ACISpeedEstimator<double> reference;
	estimator.constants = ACISpeedEstimator<T>::ComputeConstants(motor);
	reference.constants = ACISpeedEstimator<double>::ComputeConstants(motorRef);
	estimator.baseRpm = 3000;
	reference.baseRpm = 3000;

	double maxSpeed = 0;
	double maxRpm = 0;
	double maxRpmRounding = 0;
	for (int k = 0; k < steps; k++) {
		double angle = rateHz * k * ts + 0.0123;
		double theta = Q<T>(angle - std::floor(angle));
		double psiDr = Q<T>(std::cos(2 * pi * theta));
		double psiQr = Q<T>(std::sin(2 * pi * theta));
		double iqs = Q<T>(0.8);
		double ids = Q<T>(0.5);

		estimator.Set(FOCCheck::FromDouble<T>(iqs), FOCCheck::FromDouble<T>(ids), FOCCheck::FromDouble<T>(psiDr), FOCCheck::FromDouble<T>(psiQr), FOCCheck::FromDouble<T>(theta)).Resolve();
		reference.Set(iqs, ids, psiDr, psiQr, theta).Resolve();

		double speed = FOCCheck::ToDouble(estimator.Get().speed);
		maxSpeed = std::fmax(maxSpeed, std::fabs(speed - reference.Get().speed));
		maxRpm = std::fmax(maxRpm, std::fabs(static_cast<double>(estimator.Get().speedRpm) - reference.Get().speedRpm));
		// The rpm of the class is the speed times baseRpm, truncated toward zero, so it is within one count of the product.
		maxRpmRounding = std::fmax(maxRpmRounding, std::fabs(static_cast<double>(estimator.Get().speedRpm) - speed * 3000.0));
	}

	char label[96];
	std::snprintf(label, sizeof label, "ACISpeedEstimator speed %s", name);
	FOCCheck::Report(label, maxSpeed, bound, __LINE__);
	std::snprintf(label, sizeof label, "ACISpeedEstimator rpm against the reference %s", name);
	FOCCheck::Report(label, maxRpm, bound * 3000.0 + 1.0, __LINE__);
	std::snprintf(label, sizeof label, "ACISpeedEstimator rpm against the class speed %s", name);
	FOCCheck::Report(label, maxRpmRounding, 1.0, __LINE__);
}


// Physical check, independent of the reference: the flux angle turns at rateHz, so the speed settles at rateHz / fb in pu.
// The sampling period is 2^-13 s, which every format holds exactly, so the check does not depend on the setup precision.
// The rotation is 25.6 Hz at fb 50 Hz (320 steps per turn), so the window of 1600
// steps holds five whole turns and the zero-mean slip ripple averages out.
template<typename T>
void ACISettledSpeedChecks(const char* name, double baseFrequency) {
	constexpr double exactTs = 1.0 / 8192.0;
	constexpr double rateHz = 25.6;
	typename ACISpeedEstimator<T>::MotorParameters motor{
		.Rr = SetupReal<T>(1.5),
		.Lr = SetupReal<T>(0.2),
		.fb = SetupReal<T>(baseFrequency),
		.fc = SetupReal<T>(20.0),
		.Ts = SetupReal<T>(exactTs)
	};

	ACISpeedEstimator<T> estimator;
	estimator.constants = ACISpeedEstimator<T>::ComputeConstants(motor);
	estimator.baseRpm = 3000;

	// One differentiator step of one angle LSB: K2 times one LSB of T at 1.
	double settledTolerance = FOCCheck::ToDouble(estimator.constants.K2) * Lsb<T>();
	double sumSpeed = 0;
	int settledCount = 0;
	for (int k = 0; k < steps; k++) {
		double angle = rateHz * k * exactTs + 0.0123;
		double theta = Q<T>(angle - std::floor(angle));
		double psiDr = Q<T>(std::cos(2 * pi * theta));
		double psiQr = Q<T>(std::sin(2 * pi * theta));

		estimator.Set(T(0.8), T(0.5), FOCCheck::FromDouble<T>(psiDr), FOCCheck::FromDouble<T>(psiQr), FOCCheck::FromDouble<T>(theta)).Resolve();
		if (k >= settleStart && k < settleEnd) {
			sumSpeed += FOCCheck::ToDouble(estimator.Get().speed);
			settledCount++;
		}
	}

	char label[96];
	double settledSpeed = sumSpeed / settledCount;
	std::snprintf(label, sizeof label, "ACISpeedEstimator settled speed is rate / fb %s", name);
	FOCCheck::Report(label, std::fabs(settledSpeed - rateHz / baseFrequency), settledTolerance, __LINE__);
}


// Setup at Ts = 100 us: the reference is the exact double value of the physical motor, so the error is only the setup
// precision of T and the rounding of the constant. Before the setup values were SetupReal<T>, Ts was rounded to Q16 and Kr
// was about 7 percent off.
template<typename T>
void CurrentModelSetupChecks(const char* name) {
	typename CurrentModel<T>::MotorParameters motor{
		.Rr = SetupReal<T>(1.5),
		.Lr = SetupReal<T>(0.2),
		.fb = SetupReal<T>(50.0),
		.Ts = SetupReal<T>(ts)
	};
	double tr = 0.2 / 1.5;
	typename CurrentModel<T>::Constants constants = CurrentModel<T>::ComputeConstants(motor);
	char label[96];
	std::snprintf(label, sizeof label, "CurrentModel Kr from Ts 100 us %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(constants.Kr) - ts / tr), ConstantTolerance<T>(ts / tr), __LINE__);
	std::snprintf(label, sizeof label, "CurrentModel Kt from Ts 100 us %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(constants.Kt) - 1.0 / (tr * 2.0 * pi * 50.0)), ConstantTolerance<T>(1.0 / (tr * 2.0 * pi * 50.0)), __LINE__);
	std::snprintf(label, sizeof label, "CurrentModel K from Ts 100 us %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(constants.K) - ts * 50.0), ConstantTolerance<T>(ts * 50.0), __LINE__);
}


// Physical base values: Vb = 311 V is above the range of IQ<24> (below 128), so it is held by the setup values in SetupReal<T>.
// K5 = Ib Rs / Vb and K6 = Vb Ts / (Lm Ib) are the exact physical values, and the class must accept the motor at all.
template<typename T>
void ACIFluxPhysicalChecks(const char* name) {
	typename ACIFluxEstimator<T>::MotorParameters motor{
		.Rs = SetupReal<T>(2.0),
		.Rr = SetupReal<T>(1.5),
		.Ls = SetupReal<T>(0.2),
		.Lr = SetupReal<T>(0.2),
		.Lm = SetupReal<T>(0.19),
		.Ib = SetupReal<T>(4.0),
		.Vb = SetupReal<T>(311.0),
		.Ts = SetupReal<T>(ts)
	};
	typename ACIFluxEstimator<T>::Constants constants = ACIFluxEstimator<T>::ComputeConstants(motor);
	double k5 = 4.0 * 2.0 / 311.0;
	double k6 = 311.0 * ts / (0.19 * 4.0);
	char label[96];
	std::snprintf(label, sizeof label, "ACIFluxEstimator K5 from Vb 311 V %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(constants.K5) - k5), ConstantTolerance<T>(k5), __LINE__);
	std::snprintf(label, sizeof label, "ACIFluxEstimator K6 from Vb 311 V %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(constants.K6) - k6), ConstantTolerance<T>(k6), __LINE__);
}

}


// ACIFluxEstimator and SlidingModeObserver are constrained by RangeAtLeast<T, 6.3> (2*pi must be representable). The
// predicate decides what they accept; GCC reports the constraint itself as a hard error rather than false, so the class
// is not instantiated here.
static_assert(RangeAtLeast<IQ<28>, 6.3>, "IQ28 holds 2*pi");
static_assert(!RangeAtLeast<IQ<29>, 6.3>, "IQ29 does not hold 2*pi");
static_assert(RangeAtLeast<float, 6.3>, "float holds 2*pi");


int main() {
	CurrentModelChecks<float>("float", 1e-5);
	CurrentModelChecks<IQ<16>>("IQ16", 3e-2);
	CurrentModelChecks<IQ<20>>("IQ20", 2e-3);
	CurrentModelChecks<IQ<24>>("IQ24", 3e-5);
	CurrentModelStartChecks<float>("float", 1e-5);
	CurrentModelStartChecks<IQ<16>>("IQ16", 3e-2);
	CurrentModelStartChecks<IQ<20>>("IQ20", 2e-3);
	CurrentModelStartChecks<IQ<24>>("IQ24", 3e-5);

	ACIFluxChecks<float>("float", 1e-5);
	ACIFluxChecks<IQ<16>>("IQ16", 5e-3);
	ACIFluxChecks<IQ<20>>("IQ20", 1e-4);
	ACIFluxChecks<IQ<24>>("IQ24", 2e-5);

	SlidingModeChecks<float>("float", 1e-5);
	SlidingModeChecks<IQ<16>>("IQ16", 2e-4);
	SlidingModeChecks<IQ<20>>("IQ20", 3e-5);
	SlidingModeChecks<IQ<24>>("IQ24", 1e-5);

	ACISpeedChecks<float>("float, fb 50 Hz, 25 Hz", 1e-5, 50.0, 25.0);
	ACISpeedChecks<IQ<16>>("IQ16, fb 50 Hz, 25 Hz", 1e-2, 50.0, 25.0);
	ACISpeedChecks<IQ<20>>("IQ20, fb 50 Hz, 25 Hz", 1e-3, 50.0, 25.0);
	ACISpeedChecks<IQ<22>>("IQ22, fb 50 Hz, 25 Hz", 1e-3, 50.0, 25.0);
	// fb = 100 Hz gives K2 = 100, which fits IQ24 (below 128). At 50 Hz the speed is 0.5 pu, so the rpm is 1500 and the
	// product with baseRpm 3000 would saturate a 32-bit Q24 product at 128 rpm; the 64-bit product keeps 1500.
	ACISpeedChecks<IQ<24>>("IQ24, fb 100 Hz, 50 Hz", 1e-4, 100.0, 50.0);
	ACISpeedChecks<float>("float, fb 100 Hz, 50 Hz", 1e-5, 100.0, 50.0);

	ACISettledSpeedChecks<float>("float, fb 50 Hz", 50.0);
	ACISettledSpeedChecks<IQ<16>>("IQ16, fb 50 Hz", 50.0);
	ACISettledSpeedChecks<IQ<20>>("IQ20, fb 50 Hz", 50.0);
	ACISettledSpeedChecks<IQ<22>>("IQ22, fb 50 Hz", 50.0);
	ACISettledSpeedChecks<IQ<24>>("IQ24, fb 100 Hz", 100.0);
	CurrentModelSetupChecks<float>("float");
	CurrentModelSetupChecks<IQ<16>>("IQ16");
	ACIFluxPhysicalChecks<float>("float");
	ACIFluxPhysicalChecks<IQ<24>>("IQ24");

	return FOCCheck::Finish();
}
