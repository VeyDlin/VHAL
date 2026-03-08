#pragma once
#include <VHAL.h>
#include <cmath>


template<typename T = float>
class SlidingModeObserver {
public:
	struct Constants {
		T Fsmopos = 0; // Motor dependent plant matrix
		T Gsmopos = 0; // Motor dependent control gain
	};

	struct MotorParameters {
		T Rs = 0; // Stator resistance (ohm)
		T Ls = 0; // Stator inductance (H)
		T Ib = 0; // Base phase current (amp)
		T Vb = 0; // Base phase voltage (volt)
		T Ts = 0; // Sampling period in sec
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

		Constants c;
		c.Fsmopos = exp((-motor.Rs / motor.Ls) * motor.Ts);
		c.Gsmopos = (motor.Vb / motor.Ib) * (T(1) / motor.Rs) * (T(1) - c.Fsmopos);
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
		out.theta = atan2(-eAlpha, eBeta) / (T(2) * T(3.14159265358979));
		if (out.theta < T(0)) {
			out.theta += T(1);
		}

		return *this;
	}


	Out Get() {
		return out;
	}
};
