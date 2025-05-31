#pragma once
#include <System/System.h>
#include "IRegisterData.h"


class IRegisterMap {
protected:
    template <uint32 Address, typename DataType>
    friend class RegisterData;

protected:
    virtual const uint8* GetMemoryUnsafe(uint32 address) const = 0;
    virtual size_t Size() const = 0;
    virtual void LinkRegisterData(IRegisterData* registerData) = 0;
public:
    virtual size_t GetUnsafe(uint32 address, uint8* outData) = 0;
    virtual bool UpdateMemory(uint32 address, const uint8* buffer, size_t length) = 0;

    template<typename... Args>
    inline void RegisterData(Args&... args) {
        static_assert(
            (std::is_base_of_v<IRegisterData, std::remove_reference_t<Args>> && ...),
            "All arguments must derive from IRegisterData"
            );

        (args.LinkToMap(*this), ...);
        (LinkRegisterData(&args), ...);
    }
};
