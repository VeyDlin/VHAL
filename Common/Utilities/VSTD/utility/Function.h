#pragma once

#include "../memory/Handle.h"
#include "../memory/IAllocator.h"
#include "../memory/DefaultHeap.h"
#include <cstddef>
#include <string.h>
#include <new>
#include <type_traits>

namespace VSTD {

template<typename Sig>
class Function;

template<typename Ret, typename... Args>
class Function<Ret(Args...)> {
private:
    static constexpr std::size_t SboSize = 32;

    struct ICallable {
        virtual Ret Invoke(Args... args) = 0;
        virtual void Destroy() = 0;
        virtual ~ICallable() = default;
    };

    template<typename F>
    struct Callable : ICallable {
        F func;
        Callable(F&& f) : func(static_cast<F&&>(f)) {}
        Ret Invoke(Args... args) override {
            return func(static_cast<Args&&>(args)...);
        }
        void Destroy() override { func.~F(); }
    };

    alignas(std::max_align_t) unsigned char sbo_[SboSize];
    ICallable* callable_ = nullptr;
    Handle heapHandle_;
    IAllocator* allocator_ = nullptr;
    bool usesHeap_ = false;

public:
    Function() = default;

    template<typename F, typename = std::enable_if_t<
        !std::is_same_v<std::decay_t<F>, Function>>>
    Function(F&& f) {
        using Decay = std::decay_t<F>;
        if constexpr (sizeof(Callable<Decay>) <= SboSize) {
            callable_ = new (sbo_) Callable<Decay>(static_cast<F&&>(f));
            usesHeap_ = false;
        } else {
            auto* alloc = GetAlloc();
            if (alloc) {
                heapHandle_ = alloc->Allocate(sizeof(Callable<Decay>));
                if (heapHandle_.IsValid()) {
                    callable_ = new (heapHandle_.Data()) Callable<Decay>(static_cast<F&&>(f));
                    usesHeap_ = true;
                }
            }
        }
    }

    ~Function() { Cleanup(); }


    // Non-copyable
    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;

    // Move constructor
    Function(Function&& other) noexcept {
        if (other.usesHeap_) {
            callable_ = other.callable_;
            heapHandle_ = other.heapHandle_;
            allocator_ = other.allocator_;
            usesHeap_ = true;
        } else if (other.callable_) {
            memcpy(sbo_, other.sbo_, SboSize);
            auto offset = reinterpret_cast<unsigned char*>(other.callable_) - other.sbo_;
            callable_ = reinterpret_cast<ICallable*>(sbo_ + offset);
            usesHeap_ = false;
        }
        other.callable_ = nullptr;
        other.usesHeap_ = false;
        other.heapHandle_ = Handle{};
    }

    // Move assignment
    Function& operator=(Function&& other) noexcept {
        if (this != &other) {
            Cleanup();
            if (other.usesHeap_) {
                callable_ = other.callable_;
                heapHandle_ = other.heapHandle_;
                allocator_ = other.allocator_;
                usesHeap_ = true;
            } else if (other.callable_) {
                memcpy(sbo_, other.sbo_, SboSize);
                auto offset = reinterpret_cast<unsigned char*>(other.callable_) - other.sbo_;
                callable_ = reinterpret_cast<ICallable*>(sbo_ + offset);
                usesHeap_ = false;
            }
            other.callable_ = nullptr;
            other.usesHeap_ = false;
            other.heapHandle_ = Handle{};
        }
        return *this;
    }


    Ret operator()(Args... args) const {
        return callable_->Invoke(static_cast<Args&&>(args)...);
    }

    explicit operator bool() const { return callable_ != nullptr; }

private:
    IAllocator* GetAlloc() const {
        return allocator_ ? allocator_ : GetDefaultHeap();
    }

    void Cleanup() {
        if (callable_) {
            callable_->Destroy();
            callable_->~ICallable();
            callable_ = nullptr;
        }
        if (usesHeap_ && heapHandle_.IsValid()) {
            auto* alloc = GetAlloc();
            if (alloc) alloc->Deallocate(heapHandle_);
            usesHeap_ = false;
        }
    }
};

}
