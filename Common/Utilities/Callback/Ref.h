#pragma once

#include <type_traits>
#include <utility>

namespace Callback {

// A non-owning reference to a callable: two words, no allocation. It is valid only for the call that receives it. Never store it in
// a member, return it, or keep it past the full expression that built it; the referenced callable must outlive the call.
template<typename Signature>
class Ref;

template<typename Result, typename... Arguments>
class Ref<Result(Arguments...)> {
public:
	using FunctionPointer = Result (*)(Arguments...);

	// Any callable object (lambda, functor), by reference. Function pointers take the overload below.
	template<typename Callable>
		requires(!std::is_pointer_v<std::remove_cvref_t<Callable>>
			&& !std::is_same_v<std::remove_cvref_t<Callable>, Ref>
			&& std::is_invocable_r_v<Result, std::remove_reference_t<Callable>&, Arguments...>)
	constexpr Ref(Callable&& callable)
		: object(const_cast<void*>(static_cast<const void*>(&callable)))
		, invoke(&InvokeObject<std::remove_reference_t<Callable>>) {
	}


	// A function pointer or a function name. A null pointer makes an empty Ref, which tests false.
	constexpr Ref(const FunctionPointer& function)
		: object(function == nullptr ? nullptr : const_cast<void*>(static_cast<const void*>(&function)))
		, invoke(&InvokeFunction) {
	}


	constexpr explicit operator bool() const {
		return object != nullptr;
	}


	constexpr Result operator()(Arguments... arguments) const {
		return invoke(object, std::forward<Arguments>(arguments)...);
	}


private:
	void* object;
	Result (*invoke)(void*, Arguments...);

	template<typename Callable>
	static Result InvokeObject(void* target, Arguments... arguments) {
		return (*static_cast<Callable*>(target))(std::forward<Arguments>(arguments)...);
	}


	static Result InvokeFunction(void* target, Arguments... arguments) {
		return (**static_cast<const FunctionPointer*>(target))(std::forward<Arguments>(arguments)...);
	}
};

}
