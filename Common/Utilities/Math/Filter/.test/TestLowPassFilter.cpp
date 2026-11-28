#include <VHAL.h>
#include <Utilities/Math/Filter/LowPassFilter.h>
#include <VhalCheck.h>
#include <cmath>


// Host tests of the first-order low-pass filter: the step response against the closed form, and the default constructor, which
// has a time constant of 10 samples.


namespace {


void CheckStepResponse() {
	LowPassFilter<float> filter(10.0f, 1.0f);
	double alpha = 10.0 / 11.0;
	for (int step = 1; step <= 30; ++step) {
		filter.Set(1.0f).Resolve();
		double expected = 1.0 - std::pow(alpha, step);
		bool near = std::fabs(filter.Get() - expected) <= 1e-5;
		VHAL_CHECK(near);
		if (!near) {
			return;
		}
	}
}


void CheckDefaultConstructor() {
	LowPassFilter<float> filter;
	for (int step = 0; step < 300; ++step) {
		filter.Set(10.0f).Resolve();
	}
	VHAL_CHECK_NEAR(filter.Get(), 10.0, 1e-3);
}


}


int main() {
	CheckStepResponse();
	CheckDefaultConstructor();
	return VhalCheck::Finish("TestLowPassFilter");
}
