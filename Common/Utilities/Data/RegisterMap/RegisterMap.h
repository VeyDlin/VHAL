#pragma once
#include <System/System.h>
#include <cstring>
#include <utility>
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
        auto [registerData, offset] = FindRegisterData(address, 0);
        return registerData ? &(map[offset]) : nullptr;
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

    std::pair<IRegisterData*, uint32> FindRegisterData(uint32 address, size_t length) const {
        uint32 currentOffset = 0;
        
        for(IRegisterData* data : mapRegisterData) {
            if(data != nullptr) {
                if(data->Addres() == address && 
                    (
                        length == 0 ||
						(data->DataTypeSize() == length &&
						currentOffset + length <= Size())
				    )
				) {
                    return {data, currentOffset};
                }
                currentOffset += data->DataTypeSize();
            }
        }
        return {nullptr, 0};
    }

public:
    size_t GetUnsafe(uint32 address, uint8* outData) override {
        auto [registerData, offset] = FindRegisterData(address, 0);
        if (registerData == nullptr) {
            return 0;
        }

        if (!registerData->read) {
            return 0;
        }

        System::CriticalSection(true);
        size_t dataSize = registerData->GetUnsafe(outData);
        System::CriticalSection(false);

        return dataSize;
    }

    bool UpdateMemory(uint32 address, const uint8* buffer, size_t length) override {
        auto [registerData, offset] = FindRegisterData(address, length);
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
        std::memcpy(&map[offset], buffer, length);
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
