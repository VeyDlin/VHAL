#pragma once
#include <VHAL.h>
#include <cmath>
#include <algorithm>


template<typename T = float>
class SpaceVectorGenerator {
private:
	struct {
		T alpha; // reference alpha-axis phase voltage
		T beta;  // reference beta-axis phase voltage
	} in;


public:
	struct Out {
		T phaseA = 0; // reference phase-a switching function
		T phaseB = 0; // reference phase-b switching function
		T phaseC = 0; // reference phase-c switching function
	} out;


	SpaceVectorGenerator& Set(T alpha, T beta) {
		in.alpha = alpha;
		in.beta = beta;

		return *this;
	}


	SpaceVectorGenerator& Resolve() {
		using std::sqrt;
		static const T SQRT3_D2 = sqrt(T(3)) / T(2);

		T tmp1 = in.beta;
		T tmp2 = (in.beta / T(2)) + (SQRT3_D2 * in.alpha);
		T tmp3 = tmp2 - tmp1;

		// sector determination
		uint8 sector = 3;
		sector = tmp2 > 0 ? sector - 1 : sector;
		sector = tmp3 > 0 ? sector - 1 : sector;
		sector = tmp1 < 0 ? 7 - sector : sector;

		switch (sector) {
			case 1:
			case 4:
				out.phaseA = tmp2;
				out.phaseB = tmp1 - tmp3;
				out.phaseC = -tmp2;
			break;

			case 2:
			case 5:
				out.phaseA = tmp3 + tmp2;
				out.phaseB = tmp1;
				out.phaseC = -tmp1;
			break;

			default:
				out.phaseA = tmp3;
				out.phaseB = -tmp3;
				out.phaseC = -(tmp1 + tmp2);
			break;
		}

		return *this;
	}


	SpaceVectorGenerator& ResolveCommonMode() {
		using std::sqrt;
		using std::max;
		using std::min;
		static const T SQRT3_D2 = sqrt(T(3)) / T(2);

		T Va = in.alpha; // Inv Clarke
		T Vb = -(in.alpha / T(2)) + (SQRT3_D2 * in.beta);
		T Vc = -(in.alpha / T(2)) - (SQRT3_D2 * in.beta);

		T Vmax = max(max(Va, Vb), Vc);
		T Vmin = min(min(Va, Vb), Vc);

		T Vcommon = (Vmax + Vmin) / T(2);

		out.phaseA = Va - Vcommon;
		out.phaseB = Vb - Vcommon;
		out.phaseC = Vc - Vcommon;

		return *this;
	}


	SpaceVectorGenerator& ResolveDiscontinuousPwmMode() {
		using std::sqrt;
		static const T SQRT3_D2 = sqrt(T(3)) / T(2);

		T Va = in.beta;
		T Vb = (in.beta / T(2)) + (SQRT3_D2 * in.alpha);
		T Vc = Vb - Va;

		// sector determination
		uint8 sector = 3;
		sector = Vb > 0 ? sector - 1 : sector;
		sector = Vc > 0 ? sector - 1 : sector;
		sector = Va < 0 ? 7 - sector : sector;


		switch (sector) {
			case 1:
			case 6:
				out.phaseA = 0;
				out.phaseB = Vc;
				out.phaseC = Vb;
			break;

			case 2:
			case 3:
				out.phaseA = -Vc;
				out.phaseB = 0;
				out.phaseC = Va;
			break;

			default:
				out.phaseA = -Vb;
				out.phaseB = -Va;
				out.phaseC = 0;
			break;
		}

		out.phaseA = (out.phaseA * T(2)) - T(1);
		out.phaseB = (out.phaseB * T(2)) - T(1);
		out.phaseC = (out.phaseC * T(2)) - T(1);

		return *this;
	}


	Out Get() {
		return out;
	}
};
