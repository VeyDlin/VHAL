#include <Utilities/Callback/Ref.h>
#include <cstddef>
#include <cstdio>
#include <memory>

namespace {


int failures = 0;


void Expect(bool condition, const char* name) {
	if (!condition) {
		std::printf("FAILED: %s\n", name);
		failures++;
	}
}


int Twice(int value) {
	return value * 2;
}


void AddOne(int& value) {
	value++;
}


int Use(Callback::Ref<int(int)> fn, int value) {
	return fn(value);
}


struct Functor {
	int base;

	int operator()(int value) {
		return base + value;
	}
};


struct ConstFunctor {
	int operator()(int value) const {
		return value * 10;
	}
};


}


int main() {
	int captured = 3;
	auto lambda = [&captured](int value) {
		return captured * value;
	};
	const auto constLambda = [](int value) {
		return value + 1;
	};
	Functor functor{100};
	ConstFunctor constFunctor;
	int (*pointer)(int) = Twice;
	int (*nullPointer)(int) = nullptr;

	static_assert(sizeof(Callback::Ref<int(int)>) == 2 * sizeof(void*), "a Ref is two words");

	Expect(Use(lambda, 4) == 12, "capturing lambda");
	Expect(Use(constLambda, 4) == 5, "const lambda");
	Expect(Use(functor, 4) == 104, "functor lvalue");
	Expect(Use(constFunctor, 4) == 40, "const functor");
	Expect(Use(pointer, 4) == 8, "function pointer lvalue");
	Expect(Use(Twice, 4) == 8, "bare function name");
	Expect(Use([&captured](int value) { return captured + value; }, 4) == 7, "temporary lambda");

	Callback::Ref<int(int)> empty(nullPointer);
	Expect(!static_cast<bool>(empty), "null function pointer is empty");

	Callback::Ref<int(int)> filled(pointer);
	Expect(static_cast<bool>(filled), "function pointer is not empty");

	Callback::Ref<int(int)> fromLambda(lambda);
	Expect(static_cast<bool>(fromLambda), "lambda is never empty");

	{
		Callback::Ref<int(int)> source(lambda);
		Callback::Ref<int(int)> copied(source);
		Expect(copied(4) == 12, "copy of a Ref calls the callable");
	}

	{
		const Callback::Ref<int(int)> constSource(lambda);
		Callback::Ref<int(int)> constCopied(constSource);
		Expect(constCopied(4) == 12, "copy of a const Ref calls the callable");
	}

	{
		std::unique_ptr<Callback::Ref<int(int)>> source = std::make_unique<Callback::Ref<int(int)>>(lambda);
		Callback::Ref<int(int)> copied(*source);
		source.reset();
		Expect(copied(4) == 12, "copy of a Ref outlives the Ref it was copied from");
	}

	int value = 1;
	Callback::Ref<void(int&)> increment(AddOne);
	increment(value);
	Expect(value == 2, "reference argument is passed through");

	if (failures != 0) {
		std::printf("%d Ref check(s) failed\n", failures);
		return 1;
	}

	std::printf("TestRef: all checks passed\n");
	return 0;
}
