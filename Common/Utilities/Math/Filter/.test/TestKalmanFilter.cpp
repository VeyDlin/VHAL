#include <VHAL.h>
#include <Utilities/Math/Filter/KalmanFilter.h>
#include <VhalCheck.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>


// Host tests of the one-dimensional Kalman filter: the output against a double reference of the same recursion, and the estimate
// ahead of the first measurement, which is zero.


namespace {


void CheckMatchesReference() {
	const double processNoise = 0.01;
	const double measurementNoise = 3.0;
	const double stateTransition = 1.0;
	const double measurementMapping = 1.0;

	KalmanFilter<float> filter(0.01f, 3.0f, 1.0f, 0.0f, 1.0f);

	double x = 0.0;
	double p = 0.0;
	double maxError = 0.0;
	for (int k = 0; k < 200; ++k) {
		double z = 5.0 + 0.5 * std::sin(0.1 * k);
		double out = filter.Filter(static_cast<float>(z), 0.0f);

		if (k == 0) {
			x = z / measurementMapping;
			p = (1.0 / measurementMapping) * measurementNoise * (1.0 / measurementMapping);
		} else {
			double xPred = stateTransition * x;
			double pPred = stateTransition * p * stateTransition + processNoise;
			double gain = pPred * measurementMapping / (measurementMapping * pPred * measurementMapping + measurementNoise);
			x = xPred + gain * (z - measurementMapping * xPred);
			p = pPred - gain * measurementMapping * pPred;
		}

		maxError = std::max(maxError, std::fabs(out - x));
	}
	VHAL_CHECK(maxError < 1e-4);
}


// The storage is filled with a non-zero pattern, so an estimate the constructor leaves unset shows up as garbage.
// The fill goes through a volatile pointer: a plain memset ahead of the placement new is a dead store that GCC removes.
void CheckLastEstimateBeforeFilter() {
	alignas(KalmanFilter<float>) unsigned char storage[sizeof(KalmanFilter<float>)];
	volatile unsigned char* bytes = storage;
	for (size_t i = 0; i < sizeof(storage); ++i) {
		bytes[i] = 0xAB;
	}
	KalmanFilter<float>* filter = new (storage) KalmanFilter<float>(0.01f, 3.0f, 1.0f, 0.0f, 1.0f);
	float estimate = filter->GetLastStateEstimate();
	filter->~KalmanFilter();
	VHAL_CHECK(estimate == 0.0f);
}


}


int main() {
	CheckMatchesReference();
	CheckLastEstimateBeforeFilter();
	return VhalCheck::Finish("TestKalmanFilter");
}
