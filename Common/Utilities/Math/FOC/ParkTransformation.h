#pragma once
#include <VHAL.h>
#include <cmath>


// Rotation of alpha and beta by the angle (in turns) to d and q, and back. T is float or IQ<Q>.
// Range: the angle is in turns (0..1). Each output is a sum of two products, so it reaches twice an input: IQ<Q> needs
// the alpha and beta inputs below 2^(30 - Q) in magnitude (16384 at Q16, 64 at Q24) for the sums to hold.
template<typename T = float>
class ParkTransformation {
public:
	struct Axis {
		T alpha = 0; // stationary d-axis stator variable
		T beta = 0;  // stationary q-axis stator variable
		T angle = 0; // rotating angle
	};

	struct Park {
		T dAxis = 0; // rotating d-axis stator variable
		T qAxis = 0; // rotating q-axis stator variable
		T angle = 0; // rotating angle
	};


private:
	Park park;
	Axis axis;


public:
	ParkTransformation() {}



	ParkTransformation& SetPark(Park val) {
		park = val;
		return *this;
	}



	ParkTransformation& SetAxis(Axis val) {
		axis = val;
		return *this;
	}



	ParkTransformation& Resolve() {
		using std::cos;
		using std::sin;
		park.dAxis = (axis.alpha * cos(axis.angle)) + (axis.beta * sin(axis.angle));
		park.qAxis = (axis.beta * cos(axis.angle))  - (axis.alpha * sin(axis.angle));

		return *this;
	}



	ParkTransformation& ResolveInverse() {
		using std::cos;
		using std::sin;
		axis.alpha = (park.dAxis * cos(park.angle)) - (park.qAxis * sin(park.angle));
		axis.beta = (park.dAxis * sin(park.angle))  + (park.qAxis * cos(park.angle));

		return *this;
	}



	Park GetPark() {
		return park;
	}



	Axis GetAxis() {
		return axis;
	}

};