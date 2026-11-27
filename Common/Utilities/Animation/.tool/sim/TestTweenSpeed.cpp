#include <Utilities/Animation/Animation.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdint>
#include <cstdio>


// Host tests for TweenSpeed<T> with float and IQ types. The duration is the distance divided by the speed, in milliseconds,
// truncated. A speed or a distance that is not positive gives a zero duration, so the tween ends on its next update. A
// duration beyond the range of the time type saturates, so the tween does not end.


namespace {


int failureCount = 0;


void ExpectTrue(bool condition, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestTweenSpeed.cpp:%d: check failed\n", line);
	}
}


void ExpectNear(double actual, double expected, double bound, int line) {
	if (std::fabs(actual - expected) > bound) {
		failureCount++;
		std::fprintf(stderr, "TestTweenSpeed.cpp:%d: %.9f, expected %.9f within %.9f\n", line, actual, expected, bound);
	}
}


// From 0 to 100 at 50 units per second: 2000 ms. The tween is active at 1999 ms and ends at 2000 ms.
template<RealType T>
void TestDurationFromDistanceAndSpeed(const char* name) {
	Animation<T> animation;
	T last{};
	animation.onUpdateValue = [&last](T value) {
		last = value;
	};

	animation.Update(1000);
	animation.TweenSpeed(T(0), T(100), T(50));
	animation.Update(1000);
	animation.Update(2000);
	ExpectNear(static_cast<double>(static_cast<float>(last)), 50.0, 0.01, __LINE__);

	animation.Update(2999);
	ExpectTrue(animation.IsActive(), __LINE__);

	animation.Update(3000);
	ExpectTrue(!animation.IsActive(), __LINE__);
	ExpectNear(static_cast<double>(static_cast<float>(last)), 100.0, 0.0, __LINE__);

	std::printf("tween speed %-9s 100 units at 50 per second ends after 2000 ms\n", name);
}


// One third of a unit at 1 unit per second is 333.33 ms, which the duration truncates to 333 ms.
template<RealType T>
void TestDurationTruncates(const char* name) {
	Animation<T> animation;
	animation.Update(1000);
	animation.TweenSpeed(T(0), T(1), T(3));
	animation.Update(1000);
	animation.Update(1332);
	ExpectTrue(animation.IsActive(), __LINE__);

	animation.Update(1333);
	ExpectTrue(!animation.IsActive(), __LINE__);

	std::printf("tween speed %-9s 1 unit at 3 per second truncates to 333 ms\n", name);
}


// A speed that is not positive, and a distance that is not positive, end the tween on the next update.
template<RealType T>
void TestNonPositiveSpeedEndsAtNextUpdate(const char* name) {
	Animation<T> zeroSpeed;
	zeroSpeed.Update(1000);
	zeroSpeed.TweenSpeed(T(0), T(100), T(0));
	zeroSpeed.Update(1000);
	zeroSpeed.Update(1001);
	ExpectTrue(!zeroSpeed.IsActive(), __LINE__);

	Animation<T> zeroDistance;
	zeroDistance.Update(1000);
	zeroDistance.TweenSpeed(T(50), T(50), T(10));
	zeroDistance.Update(1000);
	zeroDistance.Update(1001);
	ExpectTrue(!zeroDistance.IsActive(), __LINE__);

	std::printf("tween speed %-9s a speed of zero ends at the next update\n", name);
}


// A distance of 1e30 at a speed of 1e-30 units per second is far beyond the range of a 64-bit millisecond count, so the duration
// saturates and the tween is still active after 4e9 ms.
void TestSaturatedDuration() {
	Animation<float> animation;
	animation.Update(1000);
	animation.TweenSpeed(0.0f, 1.0e30f, 1.0e-30f);
	animation.Update(1000);
	animation.Update(4000000000u);
	ExpectTrue(animation.IsActive(), __LINE__);

	std::printf("tween speed float     a duration beyond 64 bits saturates and stays active\n");
}


}


int main() {
	TestDurationFromDistanceAndSpeed<float>("float");
	TestDurationFromDistanceAndSpeed<IQ<16>>("IQ<16>");
	TestDurationFromDistanceAndSpeed<IQ<20>>("IQ<20>");
	TestDurationFromDistanceAndSpeed<IQ<24>>("IQ<24>");

	TestDurationTruncates<float>("float");
	TestDurationTruncates<IQ<16>>("IQ<16>");
	TestDurationTruncates<IQ<24>>("IQ<24>");

	TestNonPositiveSpeedEndsAtNextUpdate<float>("float");
	TestNonPositiveSpeedEndsAtNextUpdate<IQ<16>>("IQ<16>");

	TestSaturatedDuration();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestTweenSpeed: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestTweenSpeed: all checks passed\n");
	return 0;
}
