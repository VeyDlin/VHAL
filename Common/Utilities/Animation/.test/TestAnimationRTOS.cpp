#include <Utilities/Animation/AnimationRTOS.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>


// Host test for AnimationRTOS with float and IQ types. The OS port is the shim in shim/VHAL.h: the test runs the timer
// thread by hand and sets the clock. The tween is 100 ms from 0 to 1, so the value at 50 ms is 0.5 with a bound of 1e-6
// (float) and 1e-5 (IQ<24>, half a raw step of the fraction times one, plus rounding).


namespace {


int failureCount = 0;


void ExpectNear(double actual, double expected, double bound, int line) {
	if (!(std::fabs(actual - expected) <= bound)) {
		failureCount++;
		std::fprintf(stderr, "TestAnimationRTOS.cpp:%d: value %.9f, expected %.9f, bound %.9f\n", line, actual, expected, bound);
	}
}


void ExpectTrue(bool condition, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestAnimationRTOS.cpp:%d: condition failed\n", line);
	}
}


template<RealType T>
void RtosCase(double bound, const char* name) {
	System::clockMs = 1000;
	AnimationRTOS<T, 256> animation;
	T last{};
	animation.onUpdateValue = [&last](T value) {
		last = value;
	};

	animation.SetFps(50);
	ExpectTrue(animation.GetInterval() == std::chrono::milliseconds(20), __LINE__);

	animation.Tween(T(0), T(1), std::chrono::milliseconds(100));
	OS::Timer<256>* timer = static_cast<OS::Timer<256>*>(OS::RTOS::created);
	ExpectTrue(timer->running, __LINE__);

	OS::RTOS::created->Execute();
	System::clockMs = 1050;
	OS::RTOS::created->Execute();
	ExpectNear(static_cast<double>(static_cast<float>(last)), 0.5, bound, __LINE__);

	System::clockMs = 1100;
	OS::RTOS::created->Execute();
	ExpectTrue(!animation.IsActive(), __LINE__);
	ExpectTrue(!timer->running, __LINE__);
	ExpectNear(static_cast<double>(static_cast<float>(last)), 1.0, bound, __LINE__);
	animation.WaitEnd();

	std::printf("rtos %-9s value at 50 ms checked against 0.5, ends at %.6f with the timer stopped\n", name,
		static_cast<double>(static_cast<float>(last)));
}


}


int main() {
	RtosCase<float>(1e-6, "float");
	RtosCase<IQ<24>>(0.00001, "IQ<24>");

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}
	std::printf("TestAnimationRTOS passed\n");
	return 0;
}
