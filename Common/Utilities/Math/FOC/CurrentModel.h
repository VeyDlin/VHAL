#pragma once
#include <VHAL.h>
#include <cmath>


template<typename T = float>
class CurrentModel {
public:
	struct Constants {
		T Kr = 0; // constant using in magnetizing current calculation
		T Kt = 0; // constant using in slip calculation
		T K = 0;  // constant using in rotor flux angle calculation
	};

	struct MotorParameters {
		T Rr = 0; // Rotor resistance (ohm)
		T Lr = 0; // Rotor inductance (H)
		T fb = 0; // Base electrical frequency (Hz)
		T Ts = 0; // Sampling period (sec)
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
		static const T PI = T(3.14159265358979);

		T Tr = motor.Lr / motor.Rr;

		Constants c;
		c.Kr = motor.Ts / Tr;
		c.Kt = T(1) / (Tr * T(2) * PI * motor.fb);
		c.K = motor.Ts * motor.fb;
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
		slip = (constants.Kt * in.iqs) / magnetizingCurrent;
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
