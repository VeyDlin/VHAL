// expect: Callback::Ref
#include <Utilities/Callback/Ref.h>

int Wrong(int a, int b) {
	return a + b;
}

void Build() {
	Callback::Ref<int(int)> wrongPointer = Wrong;
	(void)wrongPointer;
}
