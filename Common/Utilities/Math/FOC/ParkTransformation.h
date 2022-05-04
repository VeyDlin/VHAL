#pragma once
#include <BSP.h>
#include <cmath>


class ParkTransformation {
public:
	struct Axis {
		float alpha = 0; // stationary d-axis stator variable
		float beta = 0;  // stationary q-axis stator variable
		float angle = 0; // rotating angle
	};

	struct Park {
		float dAxis = 0; // rotating d-axis stator variable
		float qAxis = 0; // rotating q-axis stator variable
		float angle = 0; // rotating angle
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
		park.dAxis = (axis.alpha * std::cos(axis.angle)) + (axis.beta * std::sin(axis.angle));
		park.qAxis = (axis.beta * std::cos(axis.angle))  - (axis.alpha * std::sin(axis.angle));

		return *this;
	}



	ParkTransformation& ResolveInverse() {
		axis.alpha = (park.dAxis * std::cos(park.angle)) - (park.qAxis * std::sin(park.angle));
		axis.beta = (park.dAxis * std::sin(park.angle))  + (park.qAxis * std::cos(park.angle));

		return *this;
	}


	
	Park GetPark() {
		return park;
	}



	Axis GetAxis() {
		return axis;
	}

};