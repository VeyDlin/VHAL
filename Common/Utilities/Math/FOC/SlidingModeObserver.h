#pragma once
#include <VHAL.h>
#include <cmath>
#include <Utilities/Math/NumberRange.h>


// Sensorless rotor angle of a PMSM from a sliding mode current observer. T is float or IQ<Q>.
// Range: T must hold 2*pi (the angle is divided by it), so IQ<Q> needs Q 28 or lower; the constraint is checked at compile
// time. The inputs are per-unit values below 2^(31 - Q) in magnitude.
// The setup values (MotorParameters) and ComputeConstants are in SetupReal<T>, which is float for float and IQ. Fsmopos =
// exp(-Rs Ts / Ls) and Gsmopos = Vb / Ib / Rs * (1 - Fsmopos), where 1 - Fsmopos is computed as -expm1, so its digits
// survive. Only Vb / Ib enters, so the per-sample currents must be per-unit of Ib and the voltages per-unit of Vb.
// Fsmopos is close to 1 (0.999 for Rs 2, Ls 0.2, Ts 100 us), so its LSB is a large part of 1 - Fsmopos: over 3000
// steps the angle error is about 1.4e-4 turn at Q16, 2.9e-6 at Q20 and 2e-6 at Q24.
template<typename T = float> requires RangeAtLeast<T, 6.3>
class SlidingModeObserver {
public:
	struct Constants {
		T Fsmopos = 0; // Motor dependent plant matrix
		T Gsmopos = 0; // Motor dependent control gain
	};

	struct MotorParameters {
		SetupReal<T> Rs = 0; // Stator resistance (ohm)
		SetupReal<T> Ls = 0; // Stator inductance (H)
		SetupReal<T> Ib = 0; // Base phase current (amp)
		SetupReal<T> Vb = 0; // Base phase voltage (volt)
		SetupReal<T> Ts = 0; // Sampling period in sec
	};


private:
	struct {
		T vAlpha = 0; // Stationary alpha-axis stator voltage
		T vBeta = 0;  // Stationary beta-axis stator voltage
		T iAlpha = 0; // Stationary alpha-axis stator current
		T iBeta = 0;  // Stationary beta-axis stator current
	} in;

	T estIAlpha = 0;    // Estimated stationary alpha-axis stator current
	T estIBeta = 0;     // Estimated stationary beta-axis stator current
	T iAlphaError = 0;  // Stationary alpha-axis current error
	T iBetaError = 0;   // Stationary beta-axis current error
	T eAlpha = 0;       // Stationary alpha-axis back EMF
	T eBeta = 0;        // Stationary beta-axis back EMF
	T zAlpha = 0;       // Stationary alpha-axis sliding control
	T zBeta = 0;        // Stationary beta-axis sliding control


public:
	Constants constants;
	T Kslide = 0; // Sliding control gain
	T Kslf = 0;   // Sliding control filter gain
	T E0 = T(0.5);  // Saturation limit

	struct Out {
		T theta = 0; // Compensated rotor angle
	} out;


	static Constants ComputeConstants(MotorParameters motor) {
		using std::exp;
		using std::expm1;

		// 1 - exp(x) is computed as -expm1(x), so the small step Rs / Ls * Ts keeps its digits.
		SetupReal<T> x = -(motor.Rs / motor.Ls) * motor.Ts;

		Constants c;
		c.Fsmopos = RealFrom<T>(exp(x));
		c.Gsmopos = RealFrom<T>((motor.Vb / motor.Ib) * (SetupReal<T>(1) / motor.Rs) * -expm1(x));
		return c;
	}


	SlidingModeObserver& Set(T vAlpha, T vBeta, T iAlpha, T iBeta) {
		in.vAlpha = vAlpha;
		in.vBeta = vBeta;
		in.iAlpha = iAlpha;
		in.iBeta = iBeta;
		return *this;
	}


	SlidingModeObserver& Resolve() {
		using std::min;
		using std::max;
		using std::atan2;

		// Sliding mode current observer
		estIAlpha = constants.Fsmopos * estIAlpha + constants.Gsmopos * (in.vAlpha - eAlpha - zAlpha);
		estIBeta  = constants.Fsmopos * estIBeta  + constants.Gsmopos * (in.vBeta  - eBeta  - zBeta);

		// Current errors
		iAlphaError = estIAlpha - in.iAlpha;
		iBetaError  = estIBeta  - in.iBeta;

		// Sliding control calculator
		T satAlpha = min(max(iAlphaError, -E0), E0);
		T satBeta  = min(max(iBetaError,  -E0), E0);
		zAlpha = satAlpha * (T(2) * Kslide);
		zBeta  = satBeta  * (T(2) * Kslide);

		// Sliding control filter -> back EMF calculator
		eAlpha = eAlpha + Kslf * (zAlpha - eAlpha);
		eBeta  = eBeta  + Kslf * (zBeta  - eBeta);

		// Rotor angle calculator -> Theta = atan(-Ealpha, Ebeta)
		constexpr T twoPi = T(6.28318530717958);
		out.theta = atan2(-eAlpha, eBeta) / twoPi;
		if (out.theta < T(0)) {
			out.theta += T(1);
		}

		return *this;
	}


	Out Get() {
		return out;
	}
};
