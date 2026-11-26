#include <Utilities/Callback/Delegate.h>
#include <cstdio>

namespace {


int failures = 0;


void Expect(bool condition, const char* name) {
	if (!condition) {
		std::printf("FAILED: %s\n", name);
		failures++;
	}
}


int freeValue = 0;


void RecordFree(int value) {
	freeValue = value;
}


struct Counter {
	int total = 0;

	void Add(int value) {
		total += value;
	}

	int Read() const {
		return total;
	}
};


void RawAdd(void* context, int value) {
	static_cast<Counter*>(context)->Add(value);
}


}


int main() {
	Callback::Delegate<void(int)> empty;
	Expect(!static_cast<bool>(empty), "default delegate is empty");

	Callback::Delegate<void(int)> free = Callback::Delegate<void(int)>::Bind<&RecordFree>();
	free(7);
	Expect(freeValue == 7, "free function is called");

	Counter counter;
	Callback::Delegate<void(int)> member = Callback::Delegate<void(int)>::Bind<Counter, &Counter::Add>(&counter);
	member(3);
	member(4);
	Expect(counter.total == 7, "member function is called on its object");

	Counter constCounter;
	constCounter.total = 5;
	Callback::Delegate<int()> reader = Callback::Delegate<int()>::Bind<Counter, &Counter::Read>(&constCounter);
	Expect(reader() == 5, "const member function is called");

	Counter raw;
	Callback::Delegate<void(int)> rawDelegate = Callback::Delegate<void(int)>::FromRaw(RawAdd, &raw);
	rawDelegate(9);
	Expect(raw.total == 9, "raw stub gets its context");

	Expect(member == member, "a delegate equals itself");
	Expect(member != free, "delegates with different targets differ");

	member.Reset();
	Expect(!static_cast<bool>(member), "Reset empties the delegate");

	if (failures != 0) {
		std::printf("%d Delegate check(s) failed\n", failures);
		return 1;
	}

	std::printf("TestDelegate: all checks passed\n");
	return 0;
}
