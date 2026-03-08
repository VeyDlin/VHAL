#pragma once
#include <VHAL.h>
#include <cmath>


template<typename T = float>
class ACIFluxEstimator {
public:
	struct Constants {
		T K1 = 0; // constant using in rotor flux calculation
		T K2 = 0; // constant using in rotor flux calculation
		T K3 = 0; // constant using in rotor flux calculation
		T K4 = 0; // constant using in stator current calculation
		T K5 = 0; // constant using in stator current calculation
		T K6 = 0; // constant using in stator current calculation
		T K7 = 0; // constant using in stator current calculation
		T K8 = 0; // constant using in torque calculation
	};

	struct MotorParameters {
		T Rs = 0; // Stator resistance (ohm)
		T Rr = 0; // Rotor resistance (ohm)
		T Ls = 0; // Stator inductance (H)
		T Lr = 0; // Rotor inductance (H)
		T Lm = 0; // Magnetizing inductance (H)
		T Ib = 0; // Base phase current (amp)
		T Vb = 0; // Base phase voltage (volt)
		T Ts = 0; // Sampling period in sec
	};

	struct PIGains {
		T Kp = 0;
		T Ki = 0;
	};


private:
	struct {
		T idsS = 0; // Stationary d-axis stator current
		T iqsS = 0; // Stationary q-axis stator current
		T udsS = 0; // Stationary d-axis stator voltage
		T uqsS = 0; // Stationary q-axis stator voltage
	} in;

	T idsE = 0;      // Measured current in sync. reference frame
	T fluxDrE = 0;   // Rotating d-axis rotor flux (current model)
	T fluxDrS = 0;   // Stationary d-axis rotor flux (current model)
	T fluxQrS = 0;   // Stationary q-axis rotor flux (current model)
	T fluxDsS = 0;   // Stationary d-axis stator flux (current model)
	T fluxQsS = 0;   // Stationary q-axis stator flux (current model)
	T psiDsS = 0;    // Stationary d-axis stator flux (voltage model)
	T psiQsS = 0;    // Stationary q-axis stator flux (voltage model)
	T uiDsS = 0;     // Stationary d-axis integral term
	T uiQsS = 0;     // Stationary q-axis integral term
	T uCompDsS = 0;  // Stationary d-axis compensated voltage
	T uCompQsS = 0;  // Stationary q-axis compensated voltage
	T emfDsS = 0;    // Stationary d-axis back emf
	T emfQsS = 0;    // Stationary q-axis back emf
	T oldEmf = 0;
	T sinVal = 0;
	T cosVal = 0;


public:
	Constants constants;
	PIGains piGains;

	struct Out {
		T thetaFlux = 0; // Rotor flux angle
		T psiDrS = 0;   // Stationary d-axis estimated rotor flux
		T psiQrS = 0;   // Stationary q-axis estimated rotor flux
	} out;


	static Constants ComputeConstants(MotorParameters m) {
		T Tr = m.Lr / m.Rr;

		Constants c;
		c.K1 = Tr / (Tr + m.Ts);
		c.K2 = m.Ts / (Tr + m.Ts);
		c.K3 = m.Lm / m.Lr;
		c.K4 = (m.Ls * m.Lr - m.Lm * m.Lm) / (m.Lr * m.Lm);
		c.K5 = m.Ib * m.Rs / m.Vb;
		c.K6 = m.Vb * m.Ts / (m.Lm * m.Ib);
		c.K7 = m.Lr / m.Lm;
		c.K8 = (m.Ls * m.Lr - m.Lm * m.Lm) / (m.Lm * m.Lm);
		return c;
	}


	ACIFluxEstimator& Set(T idsS, T iqsS, T udsS, T uqsS) {
		in.idsS = idsS;
		in.iqsS = iqsS;
		in.udsS = udsS;
		in.uqsS = uqsS;
		return *this;
	}


	ACIFluxEstimator& Resolve() {
		using std::sin;
		using std::cos;
		using std::atan2;

		static const T TWO_PI = T(2) * T(3.14159265358979);

		// Calculate Sine and Cosine terms
		sinVal = sin(out.thetaFlux * TWO_PI);
		cosVal = cos(out.thetaFlux * TWO_PI);

		// Park transformation on the measured stator current
		idsE = in.iqsS * sinVal + in.idsS * cosVal;

		// The current model section (Classical Rotor Flux Vector Control Equation)
		fluxDrE = constants.K1 * fluxDrE + constants.K2 * idsE;

		// Inverse park transformation on the rotor flux from the current model
		fluxDrS = fluxDrE * cosVal;
		fluxQrS = fluxDrE * sinVal;

		// Compute the stator flux based on the rotor flux from current model
		fluxDsS = constants.K3 * fluxDrS + constants.K4 * in.idsS;
		fluxQsS = constants.K3 * fluxQrS + constants.K4 * in.iqsS;

		// Conventional PI controller section
		T error = psiDsS - fluxDsS;
		uCompDsS = piGains.Kp * error + uiDsS;
		uiDsS = piGains.Kp * piGains.Ki * error + uiDsS;

		error = psiQsS - fluxQsS;
		uCompQsS = piGains.Kp * error + uiQsS;
		uiQsS = piGains.Kp * piGains.Ki * error + uiQsS;

		// Compute the estimated stator flux based on the integral of back emf
		oldEmf = emfDsS;
		emfDsS = in.udsS - uCompDsS - constants.K5 * in.idsS;
		psiDsS = psiDsS + T(0.5) * constants.K6 * (emfDsS + oldEmf);

		oldEmf = emfQsS;
		emfQsS = in.uqsS - uCompQsS - constants.K5 * in.iqsS;
		psiQsS = psiQsS + T(0.5) * constants.K6 * (emfQsS + oldEmf);

		// Estimate the rotor flux based on stator flux from the integral of back emf
		out.psiDrS = constants.K7 * psiDsS - constants.K8 * in.idsS;
		out.psiQrS = constants.K7 * psiQsS - constants.K8 * in.iqsS;

		// Compute the rotor flux angle
		out.thetaFlux = atan2(out.psiQrS, out.psiDrS) / TWO_PI;
		if (out.thetaFlux < T(0)) {
			out.thetaFlux += T(1);
		}

		return *this;
	}


	Out Get() {
		return out;
	}
};
