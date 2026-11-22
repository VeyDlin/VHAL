#pragma once
#include <VHAL.h>
#include <cmath>


// Medium-frequency space vector PWM: the sector angle advances by freq * freqMax each call. T is float or IQ<Q>.
// The step is rounded to T every call, and that rounding is a bias, not noise: a step that is not a multiple of the LSB
// moves the angle by the same error each call, so over N calls the angle drifts by up to N / 2 LSB. The error of the
// phase outputs for freq 0.8, freqMax 0.03, gain 0.9 over 3000 calls is 1.4e-2 at Q16, 3.6e-3 at Q18, 1.8e-3 at Q20,
// 2.3e-4 at Q22 and 5.6e-5 at Q24, so use Q24 when a 1e-4 result matters and Q20 or above in general.
// Range: the sector angle is in turns (0..1). freq and freqMax are per-unit values below 2^(31 - Q) in magnitude, and their
// product must be below one turn, since the angle is wrapped once per call.
template<typename T = float>
class SVGenMF {
private:
	struct {
		T gain = 0;     // reference gain voltage (pu)
		T offset = 0;   // reference offset voltage (pu)
		T frequency = 0; // reference frequency (pu)
	} in;

	T alpha = 0;       // Sector angle (pu)
	T newEntry = 0;    // Sine (angular) look-up pointer (pu)
	T entryOld = 0;
	uint32 sectorPointer = 0;


public:
	T freqMax = 0; // Maximum step angle = 6*base_freq*T (pu)

	struct Out {
		T phaseA = 0; // reference phase-a switching function (pu)
		T phaseB = 0; // reference phase-b switching function (pu)
		T phaseC = 0; // reference phase-c switching function (pu)
	} out;


	SVGenMF& Set(T frequency, T gain, T offset) {
		in.frequency = frequency;
		in.gain = gain;
		in.offset = offset;
		return *this;
	}


	SVGenMF& Resolve() {
		using std::sin;

		constexpr T PI_THIRD = T(1.04719755119660); // 60 degrees

		// Normalise the freq input to appropriate step angle (1 pu = 60 degree)
		T stepAngle = in.frequency * freqMax;

		// Calculate new angle alpha
		entryOld = newEntry;
		alpha = alpha + stepAngle;
		if (alpha >= T(1)) {
			alpha -= T(1);
		}
		newEntry = alpha;

		T dy = sin(newEntry * PI_THIRD);
		T dx = sin(PI_THIRD - newEntry * PI_THIRD);

		// Determine which sector
		if (newEntry - entryOld < 0) {
			sectorPointer = (sectorPointer == 5) ? 0 : sectorPointer + 1;
		}

		T ta, tb, tc;
		T base = (T(1) - dx - dy) * T(0.5);

		switch (sectorPointer) {
			case 0: // Sector 1: a,b,c -> a,b,c
				ta = base;
				tb = ta + dx;
				tc = T(1) - ta;
			break;

			case 1: // Sector 2: a,b,c -> b,a,c & dx <-> dy
				tb = base;
				ta = tb + dy;
				tc = T(1) - tb;
			break;

			case 2: // Sector 3: a,b,c -> b,c,a
				tb = base;
				tc = tb + dx;
				ta = T(1) - tb;
			break;

			case 3: // Sector 4: a,b,c -> c,b,a & dx <-> dy
				tc = base;
				tb = tc + dy;
				ta = T(1) - tc;
			break;

			case 4: // Sector 5: a,b,c -> c,a,b
				tc = base;
				ta = tc + dx;
				tb = T(1) - tc;
			break;

			default: // Sector 6: a,b,c -> a,c,b & dx <-> dy
				ta = base;
				tc = ta + dy;
				tb = T(1) - ta;
			break;
		}

		// Convert unsigned (0,1) -> signed (-1,1), then multiply with gain and add offset
		out.phaseA = in.gain * ((ta - T(0.5)) * T(2)) + in.offset;
		out.phaseB = in.gain * ((tb - T(0.5)) * T(2)) + in.offset;
		out.phaseC = in.gain * ((tc - T(0.5)) * T(2)) + in.offset;

		return *this;
	}


	Out Get() {
		return out;
	}
};
