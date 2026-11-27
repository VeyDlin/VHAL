#pragma once
#include <VHAL.h>
#include <cmath>
#include <Utilities/Math/NumberRange.h>


// Rotor flux angle of an induction motor from the current model. T is float or IQ<Q>.
// Range: the inputs ids, iqs and wr are per-unit values below 2^(31 - Q) in magnitude (32768 at Q16, 128 at Q24). The
// slip Kt iqs / im saturates at the top of an IQ format when the magnetizing current im is small, and is zero while im is zero.
// The constants depend on the sampling period and the motor. The setup values (MotorParameters) and ComputeConstants are in
// SetupReal<T>, which is float for float and IQ and double for double, so Ts and the motor values are not rounded to a Q.
// Each constant is converted to T once, so a constant is the nearest T to its value.
// Each constant is accurate to half an LSB, so the Q is set by the smallest one, Kr = Ts / Tr. The magnetizing current
// lags with time constant 1 / Kr steps, and Kr * 2^Q is the number of LSBs of Kr: for Kr = 7.5e-4 (Ts 100 us, Tr 0.133 s)
// that is 49 at Q16, 786 at Q20 and 12583 at Q24. The angle is an integral of K * we: its error grows with the number of
// steps. Over 3000 steps at this motor the error is about 2e-5 turn at Q24, 7e-4 at Q20 and 2.4e-2 at Q16.
template<typename T = float>
class CurrentModel {
public:
	struct Constants {
		T Kr = 0; // constant using in magnetizing current calculation
		T Kt = 0; // constant using in slip calculation
		T K = 0;  // constant using in rotor flux angle calculation
	};

	struct MotorParameters {
		SetupReal<T> Rr = 0; // Rotor resistance (ohm)
		SetupReal<T> Lr = 0; // Rotor inductance (H)
		SetupReal<T> fb = 0; // Base electrical frequency (Hz)
		SetupReal<T> Ts = 0; // Sampling period (sec)
	};


private:
	struct {
		T ids = 0; // Syn. rotating d-axis current (pu)
		T iqs = 0; // Syn. rotating q-axis current (pu)
		T wr = 0;  // Rotor electrically angular velocity (pu)
	} in;

	T magnetizingCurrent = 0; // Syn. rotating d-axis magnetizing current (pu)
	T slip = 0;
	T we = 0; // Angular freq of the stator


public:
	Constants constants;

	struct Out {
		T theta = 0; // Rotor flux angle (pu)
	} out;


	static Constants ComputeConstants(MotorParameters motor) {
		constexpr SetupReal<T> pi = SetupReal<T>(3.14159265358979);

		SetupReal<T> Tr = motor.Lr / motor.Rr;

		Constants c;
		c.Kr = RealFrom<T>(motor.Ts / Tr);
		c.Kt = RealFrom<T>(SetupReal<T>(1) / (Tr * SetupReal<T>(2) * pi * motor.fb));
		c.K = RealFrom<T>(motor.Ts * motor.fb);
		return c;
	}


	CurrentModel& Set(T ids, T iqs, T wr) {
		in.ids = ids;
		in.iqs = iqs;
		in.wr = wr;
		return *this;
	}


	CurrentModel& Resolve() {
		magnetizingCurrent += constants.Kr * (in.ids - magnetizingCurrent);

		// The slip divides by the magnetizing current, which is zero until ids is non-zero. No slip is taken then, so the
		// angle does not become inf (float) or follow a saturated quotient (IQ).
		if (magnetizingCurrent != T(0)) {
			slip = (constants.Kt * in.iqs) / magnetizingCurrent;
		} else {
			slip = T(0);
		}

		we = in.wr + slip;
		out.theta += constants.K * we;

		if (out.theta > T(1)) {
			out.theta -= T(1);
		} else if (out.theta < T(0)) {
			out.theta += T(1);
		}

		return *this;
	}


	Out Get() {
		return out;
	}
};
