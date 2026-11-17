#pragma once
#include <cmath>
#include <cstdint>
#include <stdlib.h>
#include <type_traits>
#include <utility>


namespace MathUtilities {
	template<typename type>
	type Random(type a, type b) {
		return a + rand() % (b - a + 1);
	}

	template<typename typeData, typename typeVal>
	inline void SetMin(typeData &data, typeVal val) {
		if constexpr (std::is_integral_v<typeData> && std::is_integral_v<typeVal> && !std::is_same_v<typeData, bool> && !std::is_same_v<typeVal, bool>) {
			if (std::cmp_less(val, data)) {
				data = val;
			}
		} else {
			if (val < data) {
				data = val;
			}
		}
	}

	template<typename typeData, typename typeVal>
	inline void SetMax(typeData &data, typeVal val) {
		if constexpr (std::is_integral_v<typeData> && std::is_integral_v<typeVal> && !std::is_same_v<typeData, bool> && !std::is_same_v<typeVal, bool>) {
			if (std::cmp_greater(val, data)) {
				data = val;
			}
		} else {
			if (val > data) {
				data = val;
			}
		}
	}


	template<typename type>
	type Extrapolation(type x0, type y0, type x1, type y1, type x2) {
		if constexpr (std::is_integral_v<type>) {
			// Widen the product so that it cannot overflow the integer type.
			int64_t scaled = (static_cast<int64_t>(x2) - x0) * (static_cast<int64_t>(y1) - y0);
			return static_cast<type>(y0 + scaled / (x1 - x0));
		} else {
			return y0 + (((x2 - x0) * (y1 - y0)) / (x1 - x0));
		}
	}


	template<typename type>
	type Interpolation(type x0, type y0, type x1, type y1, type x2) {
		if constexpr (std::is_integral_v<type>) {
			int64_t scaled = (static_cast<int64_t>(x2) - x0) * (static_cast<int64_t>(y1) - y0);
			return static_cast<type>(scaled / (x1 - x0) + y0);
		} else {
			return (((x2 - x0) * (y1 - y0)) / (x1 - x0)) + y0;
		}
	}
};
