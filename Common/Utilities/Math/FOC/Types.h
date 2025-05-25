#pragma once
#include <BSP.h>
#include <cmath>




struct Phase {
	float a = 0;
	float b = 0; 
	float c = 0;
	
        inline Phase2 GetPhase2() const {
                return {a, b};
        }
};


struct Phase2 {
	float a = 0;
	float b = 0;

        inline Phase GetPhase(float c = 0) const {
                return {a, b, c};
        }
	}
};


struct QuadratureDirectAxis {
	float q = 0;
	float d = 0;
};


