#pragma once
#include <VHAL.h>
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <Utilities/Math/NumberRange.h>


// Speed in per-unit and rpm from the electrical angle (0..1 turn) by differentiation and a low-pass filter. T is float or IQ<Q>.
// Range: the inputs are per-unit values below 2^(31 - Q) in magnitude. K1 = 1 / (fb Ts) must fit in T, and the user sets it:
// at fb 50 Hz and Ts 100 us it is 200, which fits up to Q 23 (IQ<23> holds up to 256; Q 22 is what the tests run). IQ<24>
// holds up to 128, so use fb above 78 Hz there. ComputeK1 takes fb and Ts in SetupReal<T> (float for float and IQ, double
// for double) and rounds K1 to T once, so Ts is not rounded to a Q before K1 is computed.
// The rpm is baseSpeedRpm times the speed, computed in 64 bits for IQ, so a Q24 rpm of 1800 does not saturate.
// Against the double instance over 3000 steps the speed error is about 5e-5 pu at Q16, 4e-6 at Q20 and 9e-7 at Q22.
template<typename T = float>
class SpeedCalculator {
private:
    T electricalAngle = 0;
    T oldElectricalAngle = 0;

public:
    uint32 baseSpeedRpm = 0; 	// Base speed in rpm
    T K1 = 0;       		// Constant for differentiator
    T K2 = 0;       		// Constant for low-pass filter (pu)
    T K3 = 0;       		// Constant for low-pass filter (pu)


    struct Out {
    	int32 speedRpm = 0; // Speed in rpm
        T speed = 0;    // Speed in per-unit (pu)
    } out;


    SpeedCalculator& Set(T angle) {
    	electricalAngle = angle;
    	return *this;
    }


    // K1 = 1 / (fb * Ts) for the differentiator, where fb is the base electrical frequency in Hz and Ts the sampling
    // period in s. The product is taken in SetupReal<T> and converted once, so K1 is not a product of quantized values.
    // The result is not checked against the range of T: when 1 / (fb Ts) is at or above 2^(31 - Q), an IQ K1 saturates
    // silently to the top of the format, and the speed then reads low with no other sign.
    // The caller must pick fb and Ts so that K1 fits.
    static T ComputeK1(SetupReal<T> baseFrequency, SetupReal<T> samplingPeriod) {
    	return T(SetupReal<T>(1) / (baseFrequency * samplingPeriod));
    }


    SpeedCalculator& Resolve() {
		// === Differentiator

		// Synchronous speed computation
    	T speed;
		if ((electricalAngle < T(0.9)) & (electricalAngle > T(0.1))) {
			speed = K1 * (electricalAngle - oldElectricalAngle);
		} else {
			speed = out.speed;
		}


		// === Low-pass filter

		speed = (K2 * out.speed) + (K3 * speed);

		// Saturate the output
		using std::min;
		using std::max;
		out.speed = min(max(speed, T(-1)), T(1));

		// Update the electrical angle
		oldElectricalAngle = electricalAngle;

		// Per-unit speed to rpm. For IQ the raw value times baseSpeedRpm is taken in 64 bits, so the product does not
		// saturate the Q format; the division by 2^Q truncates toward zero, as the float cast does.
		if constexpr (std::is_floating_point_v<T>) {
			out.speedRpm = static_cast<int32>(baseSpeedRpm * out.speed);
		} else {
			int64 product = static_cast<int64>(out.speed.raw) * static_cast<int64>(baseSpeedRpm);
			out.speedRpm = static_cast<int32>(product / (int64(1) << T::q));
		}

		return *this;
	}


    Out Get() {
    	return out;
    }
};
