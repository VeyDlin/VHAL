#include <VhalCheck.h>
#include <Utilities/Callback/Ref.h>


// Host tests of a Ref to a callable object that changes state: each call reaches the object it was made from.


// A reference to an lvalue callable calls that callable each time, and its effect on the captured counter is visible afterwards.
void RefCallsLvalueCallableEachCall() {
	int counter = 0;
	auto increment = [&counter]() {
		counter++;
	};
	Callback::Ref<void()> fn(increment);
	fn();
	fn();
	VHAL_CHECK_EQ(counter, 2);
}


int main() {
	RefCallsLvalueCallableEachCall();

	return VhalCheck::Finish("TestRefCallableEffects");
}
