#pragma once
#include <BSP.h>
#include <cmath>




struct Phase {
	float a = 0;
	float b = 0; 
	float c = 0;
	
	inline Phase2 GetPhase2() {
		return Phase2(a, b);
	}
};


struct Phase2 {
	float a = 0;
	float b = 0;

	inline Phase GetPhase(float c = 0) {
		return Phase2(a, b, c);
	}
};


struct QuadratureDirectAxis {
	float q = 0;
	float d = 0;
};


