#pragma once
#include "IQ.h"


// Newton-Raphson sqrt for IQ type
// y_{n+1} = (y_n + x/y_n) / 2
template<int Q, int L>
IQ<Q, L> sqrt(IQ<Q, L> x) {
	if (x.raw <= 0) return IQ<Q, L>::FromRaw(0);

	// Scale to 64-bit: result_raw = sqrt(x_raw * 2^Q)
	int64_t scaled = static_cast<int64_t>(x.raw) << Q;

	// Initial estimate via leading bit
	int64_t y = 1;
	int64_t temp = scaled;
	while (temp > 1) {
		temp >>= 2;
		y <<= 1;
	}

	// Newton iterations on 64-bit
	for (int i = 0; i < 6; i++) {
		if (y == 0) break;
		y = (y + scaled / y) >> 1;
	}

	return IQ<Q, L>::FromRaw(static_cast<int32_t>(y));
}
