#pragma once
#include <System/System.h>
#include <cstring>
#include "IRegisterMap.h"
#include "RegisterData.h"


template <typename AddressType, size_t MapSize, size_t BufferSize>
class RegisterMap : public IRegisterMap {
protected:
    uint8 map[MapSize];
    uint8 mapBuffer[BufferSize];

public:
    bool UpdateMemory(uint32 address, const uint8* buffer, size_t length) override {
        System::CriticalSection(true);
        std::memcpy(&map[address], buffer, length);
        System::CriticalSection(false);
        return true; // TODO: Add check for address range and length
    }

    template <typename DataType>
    const DataType GetMemory(uint32 address) const override {
        DataType data;
        System::CriticalSection(true);
        std::memcpy(&data, &(map[address]), sizeof(DataType));
        System::CriticalSection(false);
        return data;
    }

    inline size_t Size() const override {
        return MapSize;
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