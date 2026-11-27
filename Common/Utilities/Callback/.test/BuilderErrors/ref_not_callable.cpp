// expect: Callback::Ref
#include <Utilities/Callback/Ref.h>

void Build() {
	Callback::Ref<int(int)> fromInt = 5;
	(void)fromInt;
}
