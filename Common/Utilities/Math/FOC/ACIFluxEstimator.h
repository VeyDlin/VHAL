#pragma once
#include <VHAL.h>
#include <cmath>
#include <Utilities/Math/NumberRange.h>


// Rotor and stator flux of an induction motor from the voltage and current models. T is float or IQ<Q>.
// Range: T must hold 2*pi (the flux angle is multiplied by it), so IQ<Q> needs Q 28 or lower; the constraint is checked at
// compile time. The inputs are per-unit values below 2^(31 - Q) in magnitude.
// The setup values (MotorParameters) and ComputeConstants are in SetupReal<T>, which is float for float and IQ, so a physical
// Vb such as 311 V is held exactly for IQ<24> as well. K5 and K6 depend on Ib and Vb only through their ratio, and the
// per-sample inputs must use the same per-unit scale: currents in Ib and voltages in Vb. Each constant is rounded to T once.
// Over 3000 steps of the reference motor (Rs 2, Rr 1.5, Ls = Lr 0.2, Lm 0.19, Ts 100 us, Ib 4 / 311) the error against
// the double instance is about 4e-6 turn at Q24, 1.3e-5 at Q20 and 2.3e-4 at Q16 for the angle, and 1.5e-5, 4.4e-5 and
// 1.6e-3 for the fluxes. The flux integrators add each step's rounding, so the error grows with the number of steps.
template<typename T = float> requires RangeAtLeast<T, 6.3>
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
		SetupReal<T> Rs = 0; // Stator resistance (ohm)
		SetupReal<T> Rr = 0; // Rotor resistance (ohm)
		SetupReal<T> Ls = 0; // Stator inductance (H)
		SetupReal<T> Lr = 0; // Rotor inductance (H)
		SetupReal<T> Lm = 0; // Magnetizing inductance (H)
		SetupReal<T> Ib = 0; // Base phase current (amp)
		SetupReal<T> Vb = 0; // Base phase voltage (volt)
		SetupReal<T> Ts = 0; // Sampling period in sec
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
		SetupReal<T> Tr = m.Lr / m.Rr;
		SetupReal<T> leakage = m.Ls * m.Lr - m.Lm * m.Lm;

		Constants c;
		c.K1 = T(Tr / (Tr + m.Ts));
		c.K2 = T(m.Ts / (Tr + m.Ts));
		c.K3 = T(m.Lm / m.Lr);
		c.K4 = T(leakage / (m.Lr * m.Lm));
		c.K5 = T(m.Ib * m.Rs / m.Vb);
		c.K6 = T(m.Vb * m.Ts / (m.Lm * m.Ib));
		c.K7 = T(m.Lr / m.Lm);
		c.K8 = T(leakage / (m.Lm * m.Lm));
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

		constexpr T TWO_PI = T(6.28318530717958);

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
