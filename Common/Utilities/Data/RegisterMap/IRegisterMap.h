#pragma once
#include <System/System.h>
#include "IRegisterData.h"


class IRegisterMap {
protected:
    template <uint32 Address, typename DataType>
    friend class RegisterData;

    virtual void UpdateMemory(uint32 address, const uint8* buffer, size_t length) = 0;
    virtual const uint8* GetMemory(uint32 address) const = 0;
    virtual size_t Size() const = 0;

public:
    inline void RegisterData(IRegisterData& data) {
        data.LinkToMap(*this);
    }
};