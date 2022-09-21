#pragma once
#include "Types.h"


class ClarkeTransformation {
private:
	struct {
		Phase phase;
		Phase2 clarke;
	} inout;


public:
	ClarkeTransformation() {}



	ClarkeTransformation& Resolve2(Phase2 phase2) {
		static float const D1_SQRT3 = 1 / std::sqrt(3.0f);

		inout.phase = phase2.GetPhase();

		inout.clarke.a = phase2.a;
		inout.clarke.b = D1_SQRT3 * (phase2.a + (2 * phase2.b));

		return *this;
	}



	ClarkeTransformation& Resolve(Phase phase) {
		static float const D1_SQRT3 = 1 / std::sqrt(3.0f);

		inout.phase = phase;

		inout.clarke.a = phase2.a;
		inout.clarke.b = D1_SQRT3 * (phase.b - phase.c);

		return *this;
	}



	ClarkeTransformation& ResolveInverse(Phase2 clarke) {
		inout.clarke = clarke;

		inout.phase.a = clarke.a;
		inout.phase.b = (sqrt(3.0f) / 2) * clarke.b - (1 / 2) * clarke.a;
		inout.phase.c = (-1 / 2) * clarke.a - (sqrt(3.0f) / 2) * clarke.b;

		return *this;
	}



	Phase2 Get() {
		return inout.clarke;
	}



	Phase GetInverse() {
		return inout.phase;
	}

};