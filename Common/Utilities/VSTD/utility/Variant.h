#pragma once

#include <cstddef>
#include <new>
#include <type_traits>

#include "../core/Move.h"

namespace VSTD {

namespace detail {

// Max of sizes
template <typename... Ts>
struct MaxSize;

template <typename T>
struct MaxSize<T> {
    static constexpr std::size_t value = sizeof(T);
};

template <typename T, typename... Rest>
struct MaxSize<T, Rest...> {
    static constexpr std::size_t rest = MaxSize<Rest...>::value;
    static constexpr std::size_t value = sizeof(T) > rest ? sizeof(T) : rest;
};

// Max of alignments
template <typename... Ts>
struct MaxAlign;

template <typename T>
struct MaxAlign<T> {
    static constexpr std::size_t value = alignof(T);
};

template <typename T, typename... Rest>
struct MaxAlign<T, Rest...> {
    static constexpr std::size_t rest = MaxAlign<Rest...>::value;
    static constexpr std::size_t value = alignof(T) > rest ? alignof(T) : rest;
};

// Type-to-index mapping
template <typename Target, std::size_t I, typename... Ts>
struct TypeIndexImpl;

template <typename Target, std::size_t I, typename T, typename... Rest>
struct TypeIndexImpl<Target, I, T, Rest...> {
    static constexpr std::size_t value =
        std::is_same<Target, T>::value ? I : TypeIndexImpl<Target, I + 1, Rest...>::value;
};

template <typename Target, std::size_t I>
struct TypeIndexImpl<Target, I> {
    static constexpr std::size_t value = static_cast<std::size_t>(-1);
};

template <typename Target, typename... Ts>
struct TypeIndex {
    static constexpr std::size_t value = TypeIndexImpl<Target, 0, Ts...>::value;
};

// Index-to-type mapping
template <std::size_t I, typename... Ts>
struct TypeAtIndex;

template <std::size_t I, typename T, typename... Rest>
struct TypeAtIndex<I, T, Rest...> {
    using type = typename TypeAtIndex<I - 1, Rest...>::type;
};

template <typename T, typename... Rest>
struct TypeAtIndex<0, T, Rest...> {
    using type = T;
};

// Check if type is in pack
template <typename Target, typename... Ts>
struct TypeInPack;

template <typename Target>
struct TypeInPack<Target> {
    static constexpr bool value = false;
};

template <typename Target, typename T, typename... Rest>
struct TypeInPack<Target, T, Rest...> {
    static constexpr bool value = std::is_same<Target, T>::value || TypeInPack<Target, Rest...>::value;
};

// Destroy helper
template <std::size_t I, typename... Ts>
struct DestroyHelper;

template <std::size_t I>
struct DestroyHelper<I> {
    static void Destroy(std::size_t, void*) {}
};

template <std::size_t I, typename T, typename... Rest>
struct DestroyHelper<I, T, Rest...> {
    static void Destroy(std::size_t index, void* storage) {
        if (index == I) {
            static_cast<T*>(storage)->~T();
        } else {
            DestroyHelper<I + 1, Rest...>::Destroy(index, storage);
        }
    }
};

// Copy construct helper
template <std::size_t I, typename... Ts>
struct CopyHelper;

template <std::size_t I>
struct CopyHelper<I> {
    static void Copy(std::size_t, void*, const void*) {}
};

template <std::size_t I, typename T, typename... Rest>
struct CopyHelper<I, T, Rest...> {
    static void Copy(std::size_t index, void* dst, const void* src) {
        if (index == I) {
            new (dst) T(*static_cast<const T*>(src));
        } else {
            CopyHelper<I + 1, Rest...>::Copy(index, dst, src);
        }
    }
};

// Move construct helper
template <std::size_t I, typename... Ts>
struct MoveHelper;

template <std::size_t I>
struct MoveHelper<I> {
    static void Move(std::size_t, void*, void*) {}
};

template <std::size_t I, typename T, typename... Rest>
struct MoveHelper<I, T, Rest...> {
    static void Move(std::size_t index, void* dst, void* src) {
        if (index == I) {
            new (dst) T(VSTD::Move(*static_cast<T*>(src)));
        } else {
            MoveHelper<I + 1, Rest...>::Move(index, dst, src);
        }
    }
};

// Visit helper
template <std::size_t I, typename... Ts>
struct VisitHelper;

template <std::size_t I>
struct VisitHelper<I> {
    template <typename Visitor, typename Storage>
    static auto Apply(std::size_t, Visitor&&, Storage*)
        -> decltype(Forward<Visitor>(std::declval<Visitor&&>())(
            *static_cast<typename TypeAtIndex<0>::type*>(nullptr))) {
        while(true) {}
    }
};

template <typename Visitor, typename First, typename... Rest>
struct VisitReturnType {
    using type = decltype(std::declval<Visitor>()(std::declval<First&>()));
};

template <std::size_t I, typename T>
struct VisitHelper<I, T> {
    template <typename Visitor, typename Storage>
    static auto Apply(std::size_t index, Visitor&& visitor, Storage* storage)
        -> decltype(Forward<Visitor>(visitor)(*static_cast<T*>(storage))) {
        return Forward<Visitor>(visitor)(*static_cast<T*>(storage));
    }
};

template <std::size_t I, typename T, typename... Rest>
struct VisitHelper<I, T, Rest...> {
    template <typename Visitor, typename Storage>
    static auto Apply(std::size_t index, Visitor&& visitor, Storage* storage)
        -> decltype(Forward<Visitor>(visitor)(*static_cast<T*>(storage))) {
        if (index == I) {
            return Forward<Visitor>(visitor)(*static_cast<T*>(storage));
        }
        return VisitHelper<I + 1, Rest...>::Apply(
            index, Forward<Visitor>(visitor), storage);
    }
};

} // namespace detail

template <typename... Ts>
class Variant {
    static_assert(sizeof...(Ts) > 0, "Variant must have at least one type");

private:
    static constexpr std::size_t StorageSize  = detail::MaxSize<Ts...>::value;
    static constexpr std::size_t StorageAlign = detail::MaxAlign<Ts...>::value;

    alignas(StorageAlign) unsigned char storage_[StorageSize];
    std::size_t index_;

public:

    Variant() : index_(0) {
        using First = typename detail::TypeAtIndex<0, Ts...>::type;
        new (&storage_) First();
    }

    template <typename T,
              typename = typename std::enable_if<detail::TypeInPack<typename std::decay<T>::type, Ts...>::value>::type>
    Variant(T&& value) : index_(detail::TypeIndex<typename std::decay<T>::type, Ts...>::value) {
        using Decayed = typename std::decay<T>::type;
        new (&storage_) Decayed(Forward<T>(value));
    }

    Variant(const Variant& other) : index_(other.index_) {
        detail::CopyHelper<0, Ts...>::Copy(index_, static_cast<void*>(&storage_),
                                            static_cast<const void*>(&other.storage_));
    }

    Variant(Variant&& other) : index_(other.index_) {
        detail::MoveHelper<0, Ts...>::Move(index_, static_cast<void*>(&storage_),
                                            static_cast<void*>(&other.storage_));
    }

    ~Variant() {
        DestroyContent();
    }

    Variant& operator=(const Variant& other) {
        if (this != &other) {
            DestroyContent();
            index_ = other.index_;
            detail::CopyHelper<0, Ts...>::Copy(index_, static_cast<void*>(&storage_),
                                                static_cast<const void*>(&other.storage_));
        }
        return *this;
    }

    Variant& operator=(Variant&& other) {
        if (this != &other) {
            DestroyContent();
            index_ = other.index_;
            detail::MoveHelper<0, Ts...>::Move(index_, static_cast<void*>(&storage_),
                                                static_cast<void*>(&other.storage_));
        }
        return *this;
    }

    template <typename T,
              typename = typename std::enable_if<detail::TypeInPack<typename std::decay<T>::type, Ts...>::value>::type>
    Variant& operator=(T&& value) {
        using Decayed = typename std::decay<T>::type;
        DestroyContent();
        index_ = detail::TypeIndex<Decayed, Ts...>::value;
        new (&storage_) Decayed(Forward<T>(value));
        return *this;
    }

    std::size_t Index() const { return index_; }

    template <typename T, typename... Args>
    T& Emplace(Args&&... args) {
        static_assert(detail::TypeInPack<T, Ts...>::value, "Type not in Variant");
        DestroyContent();
        index_ = detail::TypeIndex<T, Ts...>::value;
        T* ptr = new (&storage_) T(Forward<Args>(args)...);
        return *ptr;
    }

    void* Storage() { return static_cast<void*>(&storage_); }
    const void* Storage() const { return static_cast<const void*>(&storage_); }

private:
    void DestroyContent() {
        detail::DestroyHelper<0, Ts...>::Destroy(index_, static_cast<void*>(&storage_));
    }
};

template <typename T, typename... Ts>
bool HoldsAlternative(const Variant<Ts...>& v) {
    return v.Index() == detail::TypeIndex<T, Ts...>::value;
}

template <typename T, typename... Ts>
T& Get(Variant<Ts...>& v) {
    static_assert(detail::TypeInPack<T, Ts...>::value, "Type not in Variant");
    return *static_cast<T*>(v.Storage());
}

template <typename T, typename... Ts>
const T& Get(const Variant<Ts...>& v) {
    static_assert(detail::TypeInPack<T, Ts...>::value, "Type not in Variant");
    return *static_cast<const T*>(v.Storage());
}

template <std::size_t I, typename... Ts>
auto GetByIndex(Variant<Ts...>& v)
    -> typename detail::TypeAtIndex<I, Ts...>::type& {
    using T = typename detail::TypeAtIndex<I, Ts...>::type;
    return *static_cast<T*>(v.Storage());
}

template <std::size_t I, typename... Ts>
auto GetByIndex(const Variant<Ts...>& v)
    -> const typename detail::TypeAtIndex<I, Ts...>::type& {
    using T = typename detail::TypeAtIndex<I, Ts...>::type;
    return *static_cast<const T*>(v.Storage());
}

template <typename Visitor, typename... Ts>
auto Visit(Visitor&& visitor, Variant<Ts...>& v)
    -> decltype(detail::VisitHelper<0, Ts...>::Apply(
        v.Index(), Forward<Visitor>(visitor), v.Storage())) {
    return detail::VisitHelper<0, Ts...>::Apply(
        v.Index(), Forward<Visitor>(visitor), v.Storage());
}

template <typename Visitor, typename... Ts>
auto Visit(Visitor&& visitor, const Variant<Ts...>& v)
    -> decltype(detail::VisitHelper<0, Ts...>::Apply(
        v.Index(), Forward<Visitor>(visitor),
        const_cast<void*>(v.Storage()))) {
    return detail::VisitHelper<0, Ts...>::Apply(
        v.Index(), Forward<Visitor>(visitor),
        const_cast<void*>(v.Storage()));
}

}
