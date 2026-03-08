#pragma once
#include <VHAL.h>
#include <cmath>


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
		using std::sqrt;
		static const T D1_SQRT3 = T(1) / sqrt(T(3));

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
		using std::sqrt;
		static const T SQRT3_D2 = sqrt(T(3)) / T(2);

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