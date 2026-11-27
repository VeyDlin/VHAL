#include <Utilities/Animation/Animation.h>
#include <Utilities/Math/IQMath/IQMath.h>


// Must not compile. IQ<24> has range 128, below the spring's stiffness of 200. RejectSpringRange.cmake expects the static_assert message.
void RejectSpring() {
	Animation<IQ<24>> animation;
	animation.SetSpring(200, 20);
}
