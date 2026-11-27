// expect: Callback::Ref
#include <Utilities/Callback/Ref.h>

void Build() {
	Callback::Ref<int(int)> wrongArity = [](int a, int b) {
		return a + b;
	};
	(void)wrongArity;
}
