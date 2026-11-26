# Callback

Namespace `Callback`, in `Common/Utilities/Callback/`. Two types hold a callable without allocating: `Callback::Delegate` stores one, and `Callback::Ref` refers to one for the length of a call.

| Type | Holds | Size | Use |
|---|---|---|---|
| `Callback::Delegate<R(Args...)>` | a function pointer and a context pointer | 2 words | a callback stored in an object and called later |
| `Callback::Ref<R(Args...)>` | a reference to any callable, or a function pointer by value | 2 words | a callback given to a function that calls it before it returns |

`std::function` is used in some adapters and utilities. See the last section.

## Delegate

```cpp
#include <Utilities/Callback/Delegate.h>

struct EventLog {
	void OnEvent(std::string_view type, std::string_view data, std::string_view id);
};

EventLog log;
Callback::Delegate<void(std::string_view, std::string_view, std::string_view)> onEvent;
onEvent = Callback::Delegate<void(std::string_view, std::string_view, std::string_view)>::Bind<EventLog, &EventLog::OnEvent>(&log);
```

Ways to make a Delegate:

- `Bind<&FreeFunction>()` for a free function with the signature of the Delegate.
- `Bind<T, &T::Method>(object)` for a member function, with a pointer to the object. A `const` member function takes a `const` object pointer.
- `FromRaw(stub, context)` for a function that takes the context pointer as its first argument. The context is passed to the stub unchanged.

A Delegate has no captures. A callback that needs state takes an object as its context. A lambda with captures does not fit.

A default-constructed Delegate is empty: `static_cast<bool>` gives `false`. `Reset()` empties a Delegate. Two Delegates are equal when they have the same target and stub.

The object or the context must outlive the Delegate.

## Ref

```cpp
#include <Utilities/Callback/Ref.h>

ResultStatus ReadBody(Callback::Ref<ResultStatus(std::span<const uint8>)> onBody);

ResultStatus ReadBodyTo(std::span<uint8> buffer, uint32& size) {
	size = 0;
	return ReadBody([&buffer, &size](std::span<const uint8> piece) -> ResultStatus {
		/* copy piece into buffer, count it in size */
	});
}
```

A Ref accepts a lambda, a functor (by reference, `const` or not), a function pointer, and a function name. Each argument must be callable with the Ref's parameter types and return the Ref's result type. A mismatch fails to compile at the call site.

Rules:

- A Ref to a lambda or a functor is valid only for the call that receives it. Do not store it in a member, return it, or keep it past the full expression that built it. The compiler does not check this. The callable it refers to must still exist during the call.
- A Ref made from a function pointer or a function name holds the pointer itself, so it stays valid after the full expression that built it.
- A null function pointer makes an empty Ref, and `static_cast<bool>` gives `false`. A lambda or a functor is never empty. A function that takes a Ref and may receive an empty one checks it and returns an error.
- A Ref is two words and holds no allocation.

## Choosing

- A callback that the function calls only before it returns (a parser's visitor, a body reader) takes a `Callback::Ref`.
- A callback that the object keeps and calls later (an event handler, a request handler) is a `Callback::Delegate`.

## std::function

`std::function` stays in the adapters and utilities that already use it. A callable whose capture is larger than the implementation's small buffer allocates through `operator new`. On the device, `operator new` is `malloc`. Code that must not allocate uses a Delegate or a Ref.
