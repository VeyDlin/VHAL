#pragma once
#include <System/System.h>
#include <cstring>
#include "IRegisterMap.h"
#include "RegisterData.h"



template <typename AddressType, size_t MapSize, size_t BufferSize>
class RegisterMap : public IRegisterMap {
protected:
    alignas(uint8) uint8 map[MapSize]{ 0 };
    alignas(uint8) uint8 mapBuffer[BufferSize]{ 0 }; // TODO: implement partial updates
    IRegisterData* mapRegisterData[MapSize]{ nullptr };
    uint16 mapRegisterDataCounter = 0;

    const uint8* GetMemoryUnsafe(uint32 address) const override {
        return &(map[address]);
    }

    inline size_t Size() const override {
        return MapSize;
    }

    void LinkRegisterData(IRegisterData* registerData) override {
        if (mapRegisterDataCounter < MapSize) {
            mapRegisterData[mapRegisterDataCounter++] = registerData;
        }
        else {
            SystemAbort();
        }
    }

    IRegisterData* FindRegisterData(uint32 address, size_t length) {
        for(IRegisterData* data : mapRegisterData) {
            if(
                data != nullptr &&
                data->Addres() == address &&
                data->DataTypeSize() == length &&
                address + length <= Size()
            ) {
                return data;
            }
        }
        return nullptr;
    }
public:
    bool UpdateMemory(uint32 address, const uint8* buffer, size_t length) override {
        auto registerData = this->FindRegisterData(address, length);
        if (registerData == nullptr) {
            return false;
        }

        if (!registerData->write) {
            return false;
        }

        if (!registerData->WriteEvent(buffer)) {
            return false;
        }

        System::CriticalSection(true);
        std::memcpy(&map[address], buffer, length);
        System::CriticalSection(false);

        return true;
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
