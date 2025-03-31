#pragma once
#include <functional>
#include <memory>


template <typename>
class Function;

template <typename ReturnType, typename... Args>
class Function<ReturnType(Args...)> {
    using CallbackType = ReturnType(*)(void*, Args...);

    void* callbackPtr = nullptr;
    CallbackType callback = nullptr;

public:
    Function() noexcept = default;

    template <typename Callable, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Callable>, Function>>>
    Function(Callable&& callable) noexcept {
        callbackPtr = std::addressof(callable);
        callback = [](void* callable_ptr, Args... args) -> ReturnType {
            return (*static_cast<std::add_pointer_t<std::remove_reference_t<Callable>>>(callable_ptr))(std::forward<Args>(args)...);
        };
    }


    Function(const Function& other) noexcept : callbackPtr(other.callbackPtr), callback(other.callback) {}


    Function(Function&& other) noexcept : callbackPtr(other.callbackPtr), callback(other.callback) {
        other.callbackPtr = nullptr;
        other.callback = nullptr;
    }


    ReturnType operator()(Args... args) const {
        return callback(callbackPtr, std::forward<Args>(args)...);
    }


    constexpr explicit operator bool() const noexcept {
        return callback != nullptr;
    }


    constexpr bool operator!=(std::nullptr_t) const noexcept {
        return callbackPtr != nullptr;
    }


    constexpr bool operator==(std::nullptr_t) const noexcept {
        return callbackPtr == nullptr;
    }


    Function& operator=(const Function& other) noexcept {
        if (this != &other) {
            callbackPtr = other.callbackPtr;
            callback = other.callback;
        }
        return *this;
    }


    Function& operator=(Function&& other) noexcept {
        if (this != &other) {
            callbackPtr = other.callbackPtr;
            callback = other.callback;

            other.callbackPtr = nullptr;
            other.callback = nullptr;
        }
        return *this;
    }


    Function& operator=(std::nullptr_t) noexcept {
        callbackPtr = nullptr;
        callback = nullptr;
        return *this;
    }


    template <typename Callable, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Callable>, Function>>>
    Function& operator=(Callable&& callable) noexcept {
        callbackPtr = std::addressof(callable);
        callback = [](void* callable_ptr, Args... args) -> ReturnType {
            return (*static_cast<std::add_pointer_t<std::remove_reference_t<Callable>>>(callable_ptr))(std::forward<Args>(args)...);
        };
        return *this;
    }


    template <typename Callable>
    Function& operator=(std::reference_wrapper<Callable> f) noexcept {
        callbackPtr = std::addressof(f.get());
        callback = [](void* callable_ptr, Args... args) -> ReturnType {
            return (*static_cast<Callable*>(callable_ptr))(std::forward<Args>(args)...);
        };
        return *this;
    }
};
