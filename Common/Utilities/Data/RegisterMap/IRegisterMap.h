#pragma once
#include <System/System.h>
#include "IRegisterData.h"


class IRegisterMap {
protected:
    template <uint32 Address, typename DataType>
    friend class RegisterData;

    IRegisterData* mapRegisterData[MapSize]{ nullptr };
    uint16 mapRegisterDataCounter = 0;

protected:
    virtual bool UpdateMemory(uint32 address, const uint8* buffer, size_t length) = 0;
    virtual const uint8* GetMemoryUnsafe(uint32 address) const = 0;
    virtual size_t Size() const = 0;
    virtual void LinkRegisterData(IRegisterData* registerData) = 0;

    IRegisterData* FindRegisterData(uint32 address) {
        for(IRegisterData* data : mapRegisterData) {
            if(data != nullptr && data->Addres() == address) {
                return data;
            }
        }
        return nullptr;
    }

public:
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