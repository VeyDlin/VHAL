#pragma once
#include <BSP.h>
#include <cmath>



class ClarkeTransformation {
public:
	struct Clarke {
		float alpha = 0; // stationary d-axis stator variable
		float beta = 0;  // stationary q-axis stator variable
	};

	struct Phase {
		float a = 0; // phase-a stator variable
		float b = 0; // phase-b stator variable
		float c = 0; // phase-c stator variable
	};

	enum class CurrentsMode { 
		C2, // with 2 currents
		C3  // with 2 currents
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
		static float const D1_SQRT3 = 1 / std::sqrt(3.0f);

		clarke.alpha = phase.a;

		switch (mode) {
			case CurrentsMode::C2:
				clarke.beta = D1_SQRT3 * (phase.a + (2 * phase.b));
			break;

			case CurrentsMode::C3:
				clarke.beta = D1_SQRT3 * (phase.b - phase.c);
			break;
		}
	
		return *this;
	}



	InverseClarkeTransformation& ResolveInverse() {
		phase.a = clarke.alpha;
		phase.b = (sqrt(3.0f) / 2) * clarke.beta - (1 / 2) * clarke.alpha;
		phase.c = (-1 / 2) * clarke.alpha - (sqrt(3.0f) / 2) * clarke.beta;

		return *this;
	}



	Clarke GetClarke() {
		return clarke;
	}



	Phase GetPhase() {
		return phase;
	}

};