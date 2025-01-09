#pragma once
#include <System/System.h>
#include <cstring>
#include "IRegisterMap.h"
#include "RegisterData.h"


template <typename AddressType, size_t MapSize, size_t BufferSize>
class RegisterMap : public IRegisterMap {
protected:
    uint8 map[MapSize];
    uint8 buffer[BufferSize];

    inline void UpdateMemory(uint32 address, const uint8* buffer, size_t length) override {
        std::memcpy(&map[address], buffer, length);
    }

    inline const uint8* GetMemory(uint32 address) const override {
        return &(map[address]);
    }

    inline size_t Size() const override {
        return MapSize;
    }

public:
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