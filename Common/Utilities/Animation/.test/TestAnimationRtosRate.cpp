#include <VHAL.h>
#include <Utilities/Animation/AnimationRTOS.h>
#include <VhalCheck.h>
#include <chrono>


// Host tests of the frame rate of AnimationRTOS: a rate of zero does not crash and gives a one second interval, a rate of 60 gives a
// 16 ms interval, and a rate above 1000 is clamped to a 1 ms interval.


namespace {


void CheckSetFpsZeroNoCrash() {
	AnimationRTOS<float, 256> anim;
	anim.SetFps(0);
}


void CheckSetFpsClamped() {
	AnimationRTOS<float, 256> anim;
	anim.SetFps(0);
	VHAL_CHECK(anim.GetInterval() == std::chrono::milliseconds(1000));
	anim.SetFps(60);
	VHAL_CHECK(anim.GetInterval() == std::chrono::milliseconds(16));
	anim.SetFps(5000);
	VHAL_CHECK(anim.GetInterval() == std::chrono::milliseconds(1));
}


}


int main() {
	CheckSetFpsZeroNoCrash();
	CheckSetFpsClamped();
	return VhalCheck::Finish("TestAnimationRtosRate");
}
