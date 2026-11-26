#pragma once

#include <cstddef>

namespace Callback {

// A stored callable: a function pointer with an optional context object. It never allocates and holds no captures.
template<typename Sig>
class Delegate;

template<typename Ret, typename... Args>
class Delegate<Ret(Args...)> {
private:
	using StubFn = Ret(*)(void*, Args...);

	void* object = nullptr;
	StubFn stub = nullptr;

public:
	Delegate() = default;


	template<Ret(*FreeFunc)(Args...)>
	static Delegate Bind() {
		Delegate d;
		d.object = nullptr;
		d.stub = [](void*, Args... args) -> Ret {
			return FreeFunc(static_cast<Args&&>(args)...);
		};
		return d;
	}


	template<typename T, Ret(T::*Method)(Args...)>
	static Delegate Bind(T* obj) {
		Delegate d;
		d.object = obj;
		d.stub = [](void* o, Args... args) -> Ret {
			return (static_cast<T*>(o)->*Method)(static_cast<Args&&>(args)...);
		};
		return d;
	}


	template<typename T, Ret(T::*Method)(Args...) const>
	static Delegate Bind(const T* obj) {
		Delegate d;
		d.object = const_cast<void*>(static_cast<const void*>(obj));
		d.stub = [](void* o, Args... args) -> Ret {
			return (static_cast<const T*>(o)->*Method)(static_cast<Args&&>(args)...);
		};
		return d;
	}


	static Delegate FromRaw(StubFn fn, void* context = nullptr) {
		Delegate d;
		d.object = context;
		d.stub = fn;
		return d;
	}


	Ret operator()(Args... args) const {
		return stub(object, static_cast<Args&&>(args)...);
	}


	explicit operator bool() const {
		return stub != nullptr;
	}


	void Reset() {
		object = nullptr;
		stub = nullptr;
	}


	bool operator==(const Delegate& other) const {
		return object == other.object && stub == other.stub;
	}


	bool operator!=(const Delegate& other) const {
		return !(*this == other);
	}
};

}
