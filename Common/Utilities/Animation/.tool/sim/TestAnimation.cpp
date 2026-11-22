#include <Utilities/Animation/Animation.h>
#include <Utilities/Animation/RgbAnimation.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>


// Host tests for Animation<T> and RgbAnimation<T> with float and IQ types. Tween expectations are worked out by hand: the
// value is from + (to - from) * t with a linear curve and t = elapsed / duration. The spring expectations come from a double
// reference that follows the same update steps, written in this file.
//
// Bounds (absolute, in the value's own units):
//   tween float: 1e-5. The fraction and the value are single precision, and one float step at 100 is 7.6e-6.
//   tween IQ<16>, value range 0 to 100: half a raw step of t (2^-17) times 100 plus the rounding of the value, about 0.0008.
//   tween IQ<20>: half a raw step of t (2^-21) times 100 plus the rounding of the value, about 0.000048.
//   tween IQ<24>: half a raw step of t (2^-25) times 100 plus the rounding of the value, about 0.000003.
//   spring float: 1e-4 over one second of motion.
//   spring IQ<16>: 0.01 over one second of motion, from the rounding of the time step and of the velocity.
//   spring IQ<20>: 0.001 over one second of motion.
//   spring IQ<22> and IQ<23>: 0.001 over one second of motion. Their range is below 1000, so the time step is taken as an
//   integer fraction of 1000 ms, and a step of 1000 would saturate.


namespace {


int failureCount = 0;


void ExpectNear(double actual, double expected, double bound, int line) {
	if (!(std::fabs(actual - expected) <= bound)) {
		failureCount++;
		std::fprintf(stderr, "TestAnimation.cpp:%d: value %.9f, expected %.9f, bound %.9f\n", line, actual, expected, bound);
	}
}


void ExpectTrue(bool condition, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestAnimation.cpp:%d: condition failed\n", line);
	}
}


// Double reference of the spring step in Animation<T>::UpdateSpring.
class SpringReference {
public:
	double value = 0.0;
	double velocity = 0.0;
	double target = 0.0;
	double stiffness = 200.0;
	double damping = 20.0;
	double epsilon = 0.001;
	uint64_t lastTick = 0;
	bool active = true;


	void Step(uint64_t nowMs) {
		double dt = 0.001;
		if (lastTick != 0 && lastTick < nowMs) {
			dt = static_cast<double>(nowMs - lastTick) / 1000.0;
		}
		if (dt > 0.1) {
			dt = 0.1;
		}

		double diff = target - value;
		double force = diff * stiffness;
		velocity = velocity + force * dt;
		velocity = velocity * (1.0 - damping * dt);
		value = value + velocity * dt;

		if (std::fabs(diff) < epsilon && std::fabs(velocity) < epsilon) {
			value = target;
			active = false;
		}
		lastTick = nowMs;
	}
};


// Tween from 0 to 100 over 1000 ms. The first update after Tween latches the start, so the tween starts at the next tick.
template<RealType T>
void TweenCase(double bound, const char* name) {
	Animation<T> animation;
	T last{};
	animation.onUpdateValue = [&last](T value) {
		last = value;
	};

	animation.Tween(T(0), T(100), std::chrono::milliseconds(1000));
	animation.Update(1000);
	animation.Update(1128);
	ExpectNear(static_cast<double>(static_cast<float>(last)), 12.8, bound, __LINE__);

	animation.Update(1999);
	ExpectTrue(animation.IsActive(), __LINE__);
	ExpectNear(static_cast<double>(static_cast<float>(last)), 99.9, bound, __LINE__);

	animation.Update(2000);
	ExpectTrue(!animation.IsActive(), __LINE__);
	ExpectNear(static_cast<double>(static_cast<float>(last)), 100.0, bound, __LINE__);

	std::printf("tween %-9s 1000 ms to 100: value at 128 ms within %.9f, ends at %.6f\n", name, bound,
		static_cast<double>(static_cast<float>(last)));
}


// Spring from 0 to 1 at a 17 ms tick. The class and the reference take the same ticks; the trajectory is compared while
// both are active.
template<RealType T>
void SpringCase(double bound, const char* name) {
	Animation<T> animation;
	T last{};
	animation.onUpdateValue = [&last](T value) {
		last = value;
	};

	animation.SetSpring(T(200), T(20));
	animation.SetTarget(T(1));

	SpringReference reference;
	reference.target = 1.0;

	double worst = 0.0;
	uint64_t tick = 1000;
	for (uint32_t step = 0; step < 60; step++) {
		animation.Update(tick);
		reference.Step(tick);
		if (animation.IsActive() && reference.active) {
			double error = std::fabs(static_cast<double>(static_cast<float>(animation.GetValue())) - reference.value);
			if (error > worst) {
				worst = error;
			}
		}
		tick += 17;
	}
	ExpectNear(worst, 0.0, bound, __LINE__);

	for (uint32_t step = 0; step < 200 && animation.IsActive(); step++) {
		animation.Update(tick);
		tick += 17;
	}
	ExpectTrue(!animation.IsActive(), __LINE__);
	ExpectNear(static_cast<double>(static_cast<float>(animation.GetValue())), 1.0, bound, __LINE__);

	std::printf("spring %-9s largest error against the double reference over 1 s: %.9f (bound %.6f)\n", name, worst, bound);
}


void RgbCase() {
	RgbAnimation<float> floatRgb;
	Colors::FRgb floatColor;
	floatRgb.onUpdateValue = [&floatColor](Colors::FRgb color) {
		floatColor = color;
	};
	floatRgb.Tween(Colors::FRgb::Black(), Colors::FRgb::White(), std::chrono::milliseconds(1000));
	floatRgb.Update(1000);
	floatRgb.Update(1500);
	ExpectNear(floatColor.r, 0.5, 1e-6, __LINE__);
	ExpectNear(floatColor.g, 0.5, 1e-6, __LINE__);
	ExpectNear(floatColor.b, 0.5, 1e-6, __LINE__);

	RgbAnimation<IQ<24>> iqRgb;
	Colors::FRgb iqColor;
	iqRgb.onUpdateValue = [&iqColor](Colors::FRgb color) {
		iqColor = color;
	};
	iqRgb.Tween(Colors::FRgb(0.25f, 0.0f, 1.0f), Colors::FRgb(0.75f, 1.0f, 0.0f), std::chrono::milliseconds(1000));
	iqRgb.Update(1000);
	iqRgb.Update(1250);
	ExpectNear(iqColor.r, 0.375, 1e-5, __LINE__);
	ExpectNear(iqColor.g, 0.25, 1e-5, __LINE__);
	ExpectNear(iqColor.b, 0.75, 1e-5, __LINE__);
	std::printf("rgb at 500 ms (float) and 250 ms (IQ<24>): red %.6f and %.6f, green %.6f and %.6f\n", floatColor.r, iqColor.r,
		floatColor.g, iqColor.g);
}


}


int main() {
	TweenCase<float>(1e-5, "float");
	TweenCase<IQ<16>>(0.0008, "IQ<16>");
	TweenCase<IQ<20>>(0.00005, "IQ<20>");
	TweenCase<IQ<24>>(0.000004, "IQ<24>");

	SpringCase<float>(0.0001, "float");
	SpringCase<IQ<16>>(0.01, "IQ<16>");
	SpringCase<IQ<20>>(0.001, "IQ<20>");
	SpringCase<IQ<22>>(0.001, "IQ<22>");
	SpringCase<IQ<23>>(0.001, "IQ<23>");

	RgbCase();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}
	std::printf("TestAnimation passed\n");
	return 0;
}
