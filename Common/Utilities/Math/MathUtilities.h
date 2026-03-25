#pragma once
#include <cmath>
#include <stdlib.h>


namespace MathUtilities {
	template<typename type>
	type Random(type a, type b) {
		return a + rand() % (b - a + 1);
	}

	template<typename type>
	void SetMin(type &data, type val) {
		if (val < data) {
			data = val;
		}
	}

	template<typename type>
	void SetMax(type &data, type val) {
		if (val > data) {
			data = val;
		}
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
