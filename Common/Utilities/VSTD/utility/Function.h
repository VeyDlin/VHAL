#pragma once

#include <Utilities/Memory/Handle.h>
#include <Utilities/Memory/IAllocator.h>
#include <Utilities/VSTD/memory/DefaultHeap.h>
#include <cstddef>
#include <string.h>
#include <new>
#include <type_traits>

namespace VSTD {

template<typename Sig>
class Function;

// The callable is stored in a small buffer inside the object, or in a block of an allocator when it does not fit.
// Nothing stores the address of the callable, so the object can be moved by copying its bytes, as Vector growth
// and heap defragmentation do. The callable must tolerate being copied byte for byte.
//
// Calling an empty Function returns a value-initialized result, and does nothing for void. So the result type must
// be default constructible and not a reference: a call to a Function with any other result type does not compile.
// A Function whose block the allocator refused is empty, and operator bool reports it.
template<typename Ret, typename... Args>
class Function<Ret(Args...)> {
private:
    static constexpr std::size_t SboSize = 32;

    struct ICallable {
        virtual Ret Invoke(Args... args) = 0;
        virtual ~ICallable() = default;
    };

    template<typename F>
    struct Callable : ICallable {
        F func;
        template<typename G>
        Callable(G&& f) : func(static_cast<G&&>(f)) {}
        Ret Invoke(Args... args) override {
            return func(static_cast<Args&&>(args)...);
        }
    };

    alignas(std::max_align_t) unsigned char sbo_[SboSize];
    bool hasCallable_ = false;
    bool usesHeap_ = false;
    Memory::Handle heapHandle_;
    Memory::IAllocator* allocator_ = nullptr;

public:
    Function() = default;

    // Uses the default heap for a callable that does not fit in the small buffer.
    template<typename F, typename = std::enable_if_t<
        !std::is_same_v<std::decay_t<F>, Function>>>
    Function(F&& f) {
        Store(nullptr, static_cast<F&&>(f));
    }

    // Uses alloc for a callable that does not fit in the small buffer. The block is freed to alloc.
    template<typename F, typename = std::enable_if_t<
        !std::is_same_v<std::decay_t<F>, Function>>>
    Function(Memory::IAllocator* alloc, F&& f) {
        Store(alloc, static_cast<F&&>(f));
    }

    ~Function() { Cleanup(); }


    // Non-copyable
    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;

    // Move constructor
    Function(Function&& other) noexcept {
        TakeFrom(other);
    }

    // Move assignment
    Function& operator=(Function&& other) noexcept {
        if (this != &other) {
            Cleanup();
            TakeFrom(other);
        }
        return *this;
    }


    Ret operator()(Args... args) const {
        ICallable* target = Target();
        if (target != nullptr) {
            return target->Invoke(static_cast<Args&&>(args)...);
        }

        if constexpr (std::is_void_v<Ret>) {
            return;
        } else if constexpr (std::is_reference_v<Ret> || !std::is_default_constructible_v<Ret>) {
            static_assert(sizeof(Ret) == 0,
                "an empty Function needs a default constructible, non-reference result type");
        } else {
            return Ret{};
        }
    }

    explicit operator bool() const { return hasCallable_; }

private:
    template<typename F>
    void Store(Memory::IAllocator* alloc, F&& f) {
        using Decay = std::decay_t<F>;
        if constexpr (sizeof(Callable<Decay>) <= SboSize) {
            static_assert(alignof(Callable<Decay>) <= alignof(std::max_align_t),
                "the callable is over-aligned for the small buffer");
            new (sbo_) Callable<Decay>(static_cast<F&&>(f));
            hasCallable_ = true;
            usesHeap_ = false;
        } else {
            // Heap block data is aligned to a pointer, so an over-aligned callable cannot be placed there.
            static_assert(alignof(Callable<Decay>) <= alignof(void*),
                "the callable is over-aligned for a heap block");
            Memory::IAllocator* target = alloc != nullptr ? alloc : GetDefaultHeap();
            if (target == nullptr) {
                return;
            }

            Memory::Handle block = target->Allocate(sizeof(Callable<Decay>));
            if (!block.IsValid()) {
                return;
            }

            new (block.Data()) Callable<Decay>(static_cast<F&&>(f));
            heapHandle_ = block;
            allocator_ = target;
            hasCallable_ = true;
            usesHeap_ = true;
        }
    }


    // Takes over the callable of other and leaves other empty. A small callable is copied byte for byte.
    void TakeFrom(Function& other) {
        if (other.usesHeap_) {
            heapHandle_ = other.heapHandle_;
            allocator_ = other.allocator_;
        } else if (other.hasCallable_) {
            memcpy(sbo_, other.sbo_, SboSize);
        }

        hasCallable_ = other.hasCallable_;
        usesHeap_ = other.usesHeap_;

        other.hasCallable_ = false;
        other.usesHeap_ = false;
        other.heapHandle_ = Memory::Handle{};
        other.allocator_ = nullptr;
    }


    // The callable as its interface, or nullptr when empty. The address is computed on each call.
    ICallable* Target() const {
        if (!hasCallable_) {
            return nullptr;
        }

        if (usesHeap_) {
            return reinterpret_cast<ICallable*>(heapHandle_.Data());
        }

        return reinterpret_cast<ICallable*>(const_cast<unsigned char*>(sbo_));
    }


    void Cleanup() {
        ICallable* target = Target();
        if (target != nullptr) {
            target->~ICallable();
        }

        if (usesHeap_) {
            allocator_->Deallocate(heapHandle_);
        }

        hasCallable_ = false;
        usesHeap_ = false;
        heapHandle_ = Memory::Handle{};
        allocator_ = nullptr;
    }
};

}
