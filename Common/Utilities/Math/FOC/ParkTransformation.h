#pragma once
#include "Types.h"


class ParkTransformation {
private:
	struct {
		Phase2 phase;
		QuadratureDirectAxis park;
	} inout;


public:
	ParkTransformation() {}



	ParkTransformation& Resolve(Phase2 phase2, float angle) {
		inout.phase = phase2;

		inout.park.d = (phase.a * std::cos(angle)) + (phase.b * std::sin(angle));
		inout.park.q = (phase.b * std::cos(angle)) - (phase.a * std::sin(angle));

		return *this;
	}



	ParkTransformation& ResolveInverse(QuadratureDirectAxis park, float angle) {
		inout.park = park;

		inout.phase.a = (park.d * std::cos(angle)) - (park.q * std::sin(angle));
		inout.phase.b = (park.d * std::sin(angle)) + (park.q * std::cos(angle));

		return *this;
	}


	
	QuadratureDirectAxis Get() {
		return inout.park;
	}



	Phase2 GetInverse() {
		return inout.phase;
	}

};