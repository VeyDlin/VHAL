#include <Utilities/Animation/Animation.h>
#include <Utilities/Math/IQMath/IQ.h>


volatile uint32 sourceTick = 1000;
volatile int32 resultRaw = 0;


int main() {
	Animation<IQ<16>> animation;
	animation.SetSpring(IQ<16>(200), IQ<16>(20));
	animation.SetTarget(IQ<16>(1));
	animation.Update(sourceTick);
	animation.Update(sourceTick + 17);
	resultRaw = animation.GetValue().raw;
	return 0;
}
