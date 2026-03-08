#pragma once
#include <VHAL.h>
#include <cmath>


template<typename T = float>
class ACISpeedEstimator {
public:
	struct Constants {
		T K1 = 0; // constant using in speed computation
		T K2 = 0; // constant using in differentiator
		T K3 = 0; // constant using in low-pass filter
		T K4 = 0; // constant using in low-pass filter
	};

	struct MotorParameters {
		T Rr = 0; // Rotor resistance (ohm)
		T Lr = 0; // Rotor inductance (H)
		T fb = 0; // Base electrical frequency (Hz)
		T fc = 0; // Cut-off frequency of lowpass filter (Hz)
		T Ts = 0; // Sampling period in sec
	};


private:
	struct {
		T iqsS = 0;      // Stationary q-axis stator current
		T idsS = 0;      // Stationary d-axis stator current
		T psiDrS = 0;    // Stationary d-axis rotor flux
		T psiQrS = 0;    // Stationary q-axis rotor flux
		T thetaFlux = 0; // Rotor flux angle
	} in;

	T oldThetaFlux = 0;
	T squaredPsi = 0;
	T wSlip = 0;
	T wSyn = 0;
	T wPsi = 0;


public:
	Constants constants;
	uint32 baseRpm = 3600;

	struct Out {
		T speed = 0;     // Estimated speed in per unit
		int32 speedRpm = 0;  // Estimated speed in rpm
	} out;


	static Constants ComputeConstants(MotorParameters m) {
		static const T PI = T(3.14159265358979);

		T Tr = m.Lr / m.Rr;
		T Tc = T(1) / (T(2) * PI * m.fc);
		T Wb = T(2) * PI * m.fb;

		Constants c;
		c.K1 = T(1) / (Wb * Tr);
		c.K2 = T(1) / (m.fb * m.Ts);
		c.K3 = Tc / (Tc + m.Ts);
		c.K4 = m.Ts / (Tc + m.Ts);
		return c;
	}


	ACISpeedEstimator& Set(T iqsS, T idsS, T psiDrS, T psiQrS, T thetaFlux) {
		in.iqsS = iqsS;
		in.idsS = idsS;
		in.psiDrS = psiDrS;
		in.psiQrS = psiQrS;
		in.thetaFlux = thetaFlux;
		return *this;
	}


	ACISpeedEstimator& Resolve() {
		using std::min;
		using std::max;

		// Slip computation
		squaredPsi = in.psiDrS * in.psiDrS + in.psiQrS * in.psiQrS;
		wSlip = constants.K1 * (in.psiDrS * in.iqsS - in.psiQrS * in.idsS);
		wSlip = wSlip / squaredPsi;

		// Synchronous speed computation
		if ((in.thetaFlux < T(0.8)) && (in.thetaFlux > T(0.2))) {
			wSyn = constants.K2 * (in.thetaFlux - oldThetaFlux);
		} else {
			wSyn = wPsi;
		}

		// Low-pass filter
		wPsi = constants.K3 * wPsi + constants.K4 * wSyn;

		// Update
		oldThetaFlux = in.thetaFlux;
		T wrHat = wPsi - wSlip;

		// Limit the estimated speed between -1 and 1 per-unit
		out.speed = min(max(wrHat, T(-1)), T(1));
		out.speedRpm = baseRpm * out.speed;

		return *this;
	}


	Out Get() {
		return out;
	}
};
