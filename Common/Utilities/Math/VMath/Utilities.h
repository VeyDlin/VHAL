#pragma once
#include <cmath>
#include <stdlib.h>


namespace VMath {
	template<typename type1, typename type2, typename type3>
	type1 Clamp(type1 val, type2 min, type3 max) {
		type1 out = val < min ? min : val;
	  	return out > max ? max : out;
	}


	template<typename type>
	type Max(type a, type b) {
		return (a > b) ? a : b;
	}


	template<typename type>
	type Min(type a, type b) {
		return a < b ? a : b;
	}


	template<typename type>
	type Random(type a, type b) {
		return a + rand() % (b - a + 1);
	}


	template<typename type>
	type Extrapolation(type x0, type y0, type x1, type y1, type x2) {
		return y0 + (((x2 - x0) * (y1 - y0)) / (x1 - x0));
	}


	template<typename type>
	type Interpolation(type x0, type y0, type x1, type y1, type x2) {
		return (((x2 - x0) * (y1 - y0)) / (x1 - x0)) + y0;
	}
};
