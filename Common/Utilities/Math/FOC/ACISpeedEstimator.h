#pragma once
#include <VHAL.h>
#include <cmath>
#include <type_traits>
#include <Utilities/Math/NumberRange.h>


// Speed of an induction motor from the rotor flux: the slip from the current model plus the derivative of the flux angle.
// T is float or IQ<Q>. The rpm is an int32, baseRpm times the speed, computed in 64 bits for IQ.
// Range: the inputs are per-unit values below 2^(31 - Q) in magnitude. The limit on K2 depends on the motor and the sampling
// period, so it is set by the user, not checked here: the differentiator gain K2 = 1 / (fb Ts) must fit in T, which for
// IQ<Q> means 1 / (fb Ts) < 2^(31 - Q). At Ts 100 us and fb 50 Hz K2 is 200, which fits up to Q 23 (IQ<23> holds up to 256;
// Q 22 is what the tests run). IQ<24> holds up to 128, so there it needs fb above 78 Hz. The gain multiplies the angle's LSB,
// so a low Q adds noise to the speed: at fb 50 Hz and 25 Hz rotation the speed error is about 2e-4 pu at Q16, 2.7e-5 at Q20
// and 3.3e-6 at Q22.
// The setup values (MotorParameters) and ComputeConstants are in SetupReal<T>, which is float for float and IQ, so Ts and
// the motor values are not rounded to a Q: at Ts 100 us the constants are exact to float precision for every Q. The Q
// still limits the constants stored in T (K2 above) and the speed read back.
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
		SetupReal<T> Rr = 0; // Rotor resistance (ohm)
		SetupReal<T> Lr = 0; // Rotor inductance (H)
		SetupReal<T> fb = 0; // Base electrical frequency (Hz)
		SetupReal<T> fc = 0; // Cut-off frequency of lowpass filter (Hz)
		SetupReal<T> Ts = 0; // Sampling period in sec
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
		constexpr SetupReal<T> pi = SetupReal<T>(3.14159265358979);

		SetupReal<T> Tr = m.Lr / m.Rr;
		SetupReal<T> Tc = SetupReal<T>(1) / (SetupReal<T>(2) * pi * m.fc);
		SetupReal<T> Wb = SetupReal<T>(2) * pi * m.fb;

		Constants c;
		c.K1 = RealFrom<T>(SetupReal<T>(1) / (Wb * Tr));
		c.K2 = RealFrom<T>(SetupReal<T>(1) / (m.fb * m.Ts));
		c.K3 = RealFrom<T>(Tc / (Tc + m.Ts));
		c.K4 = RealFrom<T>(m.Ts / (Tc + m.Ts));
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

		// Per-unit speed to rpm. For IQ the raw value times baseRpm is taken in 64 bits, so the product does not
		// saturate the Q format; the division by 2^Q truncates toward zero, as the float cast does.
		if constexpr (std::is_floating_point_v<T>) {
			out.speedRpm = static_cast<int32>(baseRpm * out.speed);
		} else {
			int64 product = static_cast<int64>(out.speed.raw) * static_cast<int64>(baseRpm);
			out.speedRpm = static_cast<int32>(product / (int64(1) << T::q));
		}

		return *this;
	}


	Out Get() {
		return out;
	}
};
