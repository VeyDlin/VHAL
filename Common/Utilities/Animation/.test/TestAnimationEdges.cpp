#include <VHAL.h>
#include <Utilities/Animation/Animation.h>
#include <Utilities/Animation/RgbAnimation.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <VhalCheck.h>
#include <chrono>
#include <cmath>


// Host tests of the animation edges: a tween that starts at tick 0, every easing curve at its endpoints, a zero-duration tween, a
// spring that converges, speed-driven tweens with a zero or tiny speed, and the endpoints of an RGB tween. The probe of an IQ
// animation (a run that must end above zero) is the last check.


namespace {


void CheckTweenStartAtTickZero() {
	Animation<float> anim;
	anim.Tween(1.0f, std::chrono::milliseconds(1000));
	anim.Update(0);
	anim.Update(500);
	VHAL_CHECK_NEAR(anim.GetValue(), 0.5, 1e-3);
}


void CheckEndpointsAllEasings() {
	const Easing<float>::Curve curves[] = {
		Easing<float>::Curve::Linear,
		Easing<float>::Curve::EaseIn,
		Easing<float>::Curve::EaseOut,
		Easing<float>::Curve::EaseInOut
	};
	for (Easing<float>::Curve curve : curves) {
		VHAL_CHECK(Easing<float>::Apply(curve, 0.0f) == 0.0f);
		VHAL_CHECK(Easing<float>::Apply(curve, 1.0f) == 1.0f);
	}

	Animation<float> anim;
	anim.SetEasing(Easing<float>::Curve::EaseInOut);
	anim.Tween(2.0f, 8.0f, std::chrono::milliseconds(100));
	anim.Update(1000);
	VHAL_CHECK(anim.GetValue() == 2.0f);
	anim.Update(1100);
	VHAL_CHECK(anim.GetValue() == 8.0f);
	VHAL_CHECK(!anim.IsActive());
}


void CheckZeroDuration() {
	Animation<float> anim;
	anim.Tween(0.0f, 5.0f, std::chrono::milliseconds(0));
	anim.Update(1000);
	VHAL_CHECK(anim.GetValue() == 5.0f);
	VHAL_CHECK(!anim.IsActive());
}


void CheckSpringConverges() {
	Animation<float> anim;
	anim.Update(0);
	anim.SetTarget(1.0f);
	for (uint64 now = 16; now <= 6000; now += 16) {
		anim.Update(now);
	}
	VHAL_CHECK(std::fabs(anim.GetValue() - 1.0) <= 0.01 && !anim.IsActive());
}


void CheckTweenSpeedZero() {
	Animation<float> anim;
	anim.TweenSpeed(10.0f, 0.0f);
	anim.Update(1000);
}


void CheckTweenSpeedZeroFinishesAtTarget() {
	Animation<float> anim;
	anim.TweenSpeed(10.0f, 0.0f);
	anim.Update(1000);
	VHAL_CHECK(anim.GetValue() == 10.0f);
	VHAL_CHECK(!anim.IsActive());

	Animation<float> backwards;
	backwards.TweenSpeed(2.0f, 4.0f, -1.0f);
	backwards.Update(1000);
	VHAL_CHECK(backwards.GetValue() == 4.0f);
	VHAL_CHECK(!backwards.IsActive());
}


void CheckTweenSpeedTinySpeed() {
	Animation<float> anim;
	anim.TweenSpeed(10.0f, 1e-30f);
	anim.Update(1000);
	anim.Update(2000);
}


void CheckRgbEndpoints() {
	RgbAnimation<float> anim;
	Colors::FRgb start(0.0f, 0.5f, 1.0f);
	Colors::FRgb target(1.0f, 0.0f, 0.25f);
	anim.Tween(start, target, std::chrono::milliseconds(100));

	anim.Update(1000);
	VHAL_CHECK(anim.GetValue() == start);

	anim.Update(1050);
	Colors::FRgb middle = anim.GetValue();
	VHAL_CHECK(std::fabs(middle.r - 0.5) <= 1e-5 && std::fabs(middle.g - 0.25) <= 1e-5 && std::fabs(middle.b - 0.625) <= 1e-5);

	anim.Update(1100);
	VHAL_CHECK(anim.GetValue() == target);
	VHAL_CHECK(anim.GetEndValue() == target);
	VHAL_CHECK(!anim.IsActive());
}


// The probe of an IQ animation: the tween runs to the end and the value is above zero.
void CheckIqProbe() {
	Animation<IQ<16>> anim;
	anim.Tween(IQ<16>(1), std::chrono::milliseconds(100));
	anim.Update(1000);
	anim.Update(1050);
	VHAL_CHECK(anim.GetValue() > IQ<16>(0));
}


void CheckRgbProbe() {
	RgbAnimation<float> anim;
	anim.Update(0);
}


}


int main() {
	CheckTweenStartAtTickZero();
	CheckEndpointsAllEasings();
	CheckZeroDuration();
	CheckSpringConverges();
	CheckTweenSpeedZero();
	CheckTweenSpeedZeroFinishesAtTarget();
	CheckTweenSpeedTinySpeed();
	CheckRgbEndpoints();
	CheckIqProbe();
	CheckRgbProbe();
	return VhalCheck::Finish("TestAnimationEdges");
}
