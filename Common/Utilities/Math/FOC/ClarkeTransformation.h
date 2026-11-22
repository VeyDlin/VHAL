#pragma once
#include <VHAL.h>
#include <cmath>


// Stationary-frame transform of three phase values to alpha and beta, and back. T is float or IQ<Q>.
// Range: the phase inputs are per-unit values. beta = (a + 2 b) / sqrt(3) reaches three times an input, so IQ<Q> needs
// inputs below 2^(29 - Q) in magnitude (8192 at Q16, 32 at Q24) for the intermediate sum to hold.
template<typename T = float>
class ClarkeTransformation {
public:
	struct Clarke {
		T alpha = 0; // stationary d-axis stator variable
		T beta = 0;  // stationary q-axis stator variable
	};

	struct Phase {
		T a = 0; // phase-a stator variable
		T b = 0; // phase-b stator variable
		T c = 0; // phase-c stator variable
	};

	enum class CurrentsMode {
		C2, // with 2 currents
		C3  // with 3 currents
	};


private:
	Clarke clarke;
	Phase phase;


public:
	ClarkeTransformation() {}



	ClarkeTransformation& SetClarke(Clarke val) {
		clarke = val;
		return *this;
	}



	ClarkeTransformation& SetPhase(Phase val) {
		phase = val;
		return *this;
	}



	ClarkeTransformation& Resolve(CurrentsMode mode = CurrentsMode::C2) {
		constexpr T D1_SQRT3 = T(0.5773502691896258); // 1 / sqrt(3)

		clarke.alpha = phase.a;

		switch (mode) {
			case CurrentsMode::C2:
				clarke.beta = D1_SQRT3 * (phase.a + (T(2) * phase.b));
			break;

			case CurrentsMode::C3:
				clarke.beta = D1_SQRT3 * (phase.b - phase.c);
			break;
		}

		return *this;
	}



	ClarkeTransformation& ResolveInverse() {
		constexpr T SQRT3_D2 = T(0.8660254037844386); // sqrt(3) / 2

		phase.a = clarke.alpha;
		phase.b = SQRT3_D2 * clarke.beta - T(0.5) * clarke.alpha;
		phase.c = -T(0.5) * clarke.alpha - SQRT3_D2 * clarke.beta;

		return *this;
	}



	Clarke GetClarke() {
		return clarke;
	}



	Phase GetPhase() {
		return phase;
	}

};