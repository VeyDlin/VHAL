#pragma once

#include <cstddef>

namespace VSTD {

template<typename Sig>
class Delegate;

template<typename Ret, typename... Args>
class Delegate<Ret(Args...)> {
    using StubFn = Ret(*)(void*, Args...);

private:
    void* object_ = nullptr;
    StubFn stub_ = nullptr;

public:
    Delegate() = default;

    template<Ret(*FreeFunc)(Args...)>
    static Delegate Bind() {
        Delegate d;
        d.object_ = nullptr;
        d.stub_ = [](void*, Args... args) -> Ret {
            return FreeFunc(static_cast<Args&&>(args)...);
        };
        return d;
    }

    template<typename T, Ret(T::*Method)(Args...)>
    static Delegate Bind(T* obj) {
        Delegate d;
        d.object_ = obj;
        d.stub_ = [](void* o, Args... args) -> Ret {
            return (static_cast<T*>(o)->*Method)(static_cast<Args&&>(args)...);
        };
        return d;
    }

    template<typename T, Ret(T::*Method)(Args...) const>
    static Delegate Bind(const T* obj) {
        Delegate d;
        d.object_ = const_cast<void*>(static_cast<const void*>(obj));
        d.stub_ = [](void* o, Args... args) -> Ret {
            return (static_cast<const T*>(o)->*Method)(static_cast<Args&&>(args)...);
        };
        return d;
    }

    static Delegate FromRaw(StubFn fn, void* context = nullptr) {
        Delegate d;
        d.object_ = context;
        d.stub_ = fn;
        return d;
    }


    Ret operator()(Args... args) const {
        return stub_(object_, static_cast<Args&&>(args)...);
    }

    explicit operator bool() const { return stub_ != nullptr; }


    void Reset() {
        object_ = nullptr;
        stub_ = nullptr;
    }

    bool operator==(const Delegate& other) const {
        return object_ == other.object_ && stub_ == other.stub_;
    }

    bool operator!=(const Delegate& other) const {
        return !(*this == other);
    }
};

}
