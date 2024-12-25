#pragma once
#include <System/System.h>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

template <typename... Types>
struct MaxSize;

template <typename T, typename... Rest>
struct MaxSize<T, Rest...> {
    static constexpr size_t value = sizeof(T) > MaxSize<Rest...>::value ? sizeof(T) : MaxSize<Rest...>::value;
};

template <>
struct MaxSize<> {
    static constexpr size_t value = 0;
};

template <typename... Types>
struct MaxAlignment;

template <typename T, typename... Rest>
struct MaxAlignment<T, Rest...> {
    static constexpr size_t value = alignof(T) > MaxAlignment<Rest...>::value ? alignof(T) : MaxAlignment<Rest...>::value;
};

template <>
struct MaxAlignment<> {
    static constexpr size_t value = 0;
};

template <class, typename... Callables> class Function;

template <class ReturnType, class... Args, typename... Callables>
class Function<ReturnType(Args...), Callables...> {
public:
    static constexpr size_t MaxSize = MaxSize<Callables...>::value;
    static constexpr size_t MaxAlign = MaxAlignment<Callables...>::value;

    using Storage = typename std::aligned_storage<MaxSize, MaxAlign>::type;

    Function() noexcept {}

    Function(std::nullptr_t) noexcept {}

    Function(const Function& other) {
        if (other) {
            other.manager(data, other.data, Operation::Clone);
            invoker = other.invoker;
            manager = other.manager;
        }
    }

    Function(Function&& other) {
        other.swap(*this);
    }

    template <class F>
    Function(F&& f) {
        using f_type = typename std::decay<F>::type;
        static_assert(sizeof(f_type) <= MaxSize, "Callable size exceeds buffer size");
        static_assert(alignof(f_type) <= MaxAlign, "Callable alignment is incompatible with storage");
        new (&data) f_type(std::forward<F>(f));
        invoker = &invoke<f_type>;
        manager = &manage<f_type>;
    }

    ~Function() {
        if (manager) {
            manager(&data, nullptr, Operation::Destroy);
        }
    }

    Function& operator=(const Function& other) {
        Function(other).swap(*this);
        return *this;
    }

    Function& operator=(Function&& other) {
        Function(std::move(other)).swap(*this);
        return *this;
    }

    Function& operator=(std::nullptr_t) {
        if (manager) {
            manager(&data, nullptr, Operation::Destroy);
            manager = nullptr;
            invoker = nullptr;
        }
        return *this;
    }

    template <typename F> Function& operator=(F&& f) {
        Function(std::forward<F>(f)).swap(*this);
        return *this;
    }

    template <typename F> Function& operator=(std::reference_wrapper<F> f) {
        Function(f).swap(*this);
        return *this;
    }

    void swap(Function& other) {
        std::swap(data, other.data);
        std::swap(manager, other.manager);
        std::swap(invoker, other.invoker);
    }

    explicit operator bool() const noexcept { return !!manager; }

    ReturnType operator()(Args... args) {
        if (!invoker) {
            SystemAbort();
        }
        return invoker(&data, std::forward<Args>(args)...);
    }

private:
    enum class Operation { Clone, Destroy };

    using Invoker = ReturnType(*)(void*, Args &&...);
    using Manager = void (*)(void*, void*, Operation);

    template <typename F>
    static ReturnType invoke(void* data, Args &&... args) {
        F& f = *static_cast<F*>(data);
        return f(std::forward<Args>(args)...);
    }

    template <typename F>
    static void manage(void* dest, void* src, Operation op) {
        switch (op) {
            case Operation::Clone:
                new (dest) F(*static_cast<F*>(src));
                break;
            case Operation::Destroy:
                static_cast<F*>(dest)->~F();
                break;
        }
    }

    Storage data;
    Invoker invoker = nullptr;
    Manager manager = nullptr;
};