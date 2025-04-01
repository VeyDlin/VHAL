#pragma once
#include <System/System.h>
#include <cstring>
#include "IRegisterMap.h"
#include "RegisterData.h"


template <typename AddressType, size_t MapSize, size_t BufferSize>
class ExternalRegisterMap : public IRegisterMap {
protected:
    alignas(uint8) uint8 mapBuffer[BufferSize]{ }; // TODO: implement partial updates
    IRegisterData* mapRegisterData[MapSize]{ nullptr };
    uint16 mapRegisterDataCounter = 0;

    bool UpdateMemory(uint32 address, const uint8* buffer, size_t length) override {
        return TryUpdateMemory(address, buffer, length) != nullptr;
    }

    const uint8* GetMemoryUnsafe(uint32 address) const override {
        return nullptr;
    }

    inline size_t Size() const override {
        return MapSize;
    }

    void LinkRegisterData(IRegisterData* registerData) override {
        if(mapRegisterDataCounter < MapSize) {
            mapRegisterData[mapRegisterDataCounter++] = registerData;
        } else {
            SystemAbort();
        }
    }


public:
    IRegisterData* TryUpdateMemory(uint32 address, const uint8* buffer, size_t length) {
        auto registerData = FindRegisterData(address);
        if(registerData == nullptr) {
            return nullptr;
        }

        if(!registerData->write) {
            return nullptr;
        }

        if(registerData->Addres() != address) {
            return nullptr;
        }

        if(registerData->DataTypeSize() != length) {
            return nullptr;
        }

        if(address + length > MapSize) {
            return nullptr;
        }

        if(!registerData->WriteEvent(buffer, length)) {
            return nullptr;
        }

        return registerData;
    }

    static constexpr size_t GetAddressSize() noexcept {
        return sizeof(AddressType);
    }

    static constexpr size_t GetMapSize() noexcept {
        return MapSize;
    }

    static constexpr size_t GetBufferSize() noexcept {
        return BufferSize;
    }
};