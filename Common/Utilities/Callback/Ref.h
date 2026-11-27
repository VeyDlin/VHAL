#pragma once

#include <type_traits>
#include <utility>

namespace Callback {

// A non-owning reference to a callable: two words, no allocation. A function pointer or a function name is held by value, so
// it stays valid after the full expression that built the Ref. An object is held by reference: it is valid only for the call
// that receives the Ref, and the callable must outlive that call. Never store a Ref of an object in a member, or return it.
template<typename Signature>
class Ref;

template<typename Result, typename... Arguments>
class Ref<Result(Arguments...)> {
public:
	using FunctionPointer = Result (*)(Arguments...);

	// Any callable object (lambda, functor), by reference. Functions and function pointers take the overload below.
	template<typename Callable>
		requires(!std::is_pointer_v<std::remove_cvref_t<Callable>>
			&& !std::is_function_v<std::remove_reference_t<Callable>>
			&& !std::is_same_v<std::remove_cvref_t<Callable>, Ref>
			&& std::is_invocable_r_v<Result, std::remove_reference_t<Callable>&, Arguments...>)
	constexpr Ref(Callable&& callable)
		: target{.object = const_cast<void*>(static_cast<const void*>(&callable))}
		, invoke(&InvokeObject<std::remove_reference_t<Callable>>) {
	}


	// A function pointer or a function name, held by value. A null pointer makes an empty Ref, which tests false.
	constexpr Ref(FunctionPointer function)
		: target{.function = function}
		, invoke(function == nullptr ? nullptr : &InvokeFunction) {
	}


	constexpr explicit operator bool() const {
		return invoke != nullptr;
	}


	constexpr Result operator()(Arguments... arguments) const {
		return invoke(target, std::forward<Arguments>(arguments)...);
	}


private:
	union Target {
		void* object;
		FunctionPointer function;
	};


	Target target;
	Result (*invoke)(Target, Arguments...);

	template<typename Callable>
	static Result InvokeObject(Target reference, Arguments... arguments) {
		return (*static_cast<Callable*>(reference.object))(std::forward<Arguments>(arguments)...);
	}


	static Result InvokeFunction(Target reference, Arguments... arguments) {
		return reference.function(std::forward<Arguments>(arguments)...);
	}
};

}
