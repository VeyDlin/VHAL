#pragma once
#include <System/System.h>
#include <utility>
#include <functional>
#include "IRegisterData.h"


template <uint32 Address, typename RawDataType>
class RegisterData : public IRegisterData {
private:
    std::function<void(const RawDataType&)> onUpdate;

public:
    RegisterData(std::function<void(const RawDataType&)> event = nullptr) : onUpdate(std::move(event)) {}

    static constexpr size_t GetDataTypeSize() noexcept {
        return sizeof(RawDataType);
    }

    static constexpr uint32 GetAddress() noexcept {
        return Address;
    }

    RawDataType Get() const {
        const auto* storageRef = GetMemory();
        return static_cast<RawDataType>(*storageRef);
    }

    void Set(const RawDataType& value) {
        UpdateMemory(value);
    }

    inline RegisterData& operator=(const RawDataType& value) {
        Set(value);
        return *this;
    }

    inline operator RawDataType() const {
        return Get();
    }

protected:
    template <typename MemoryData = RawDataType>
    inline void UpdateMemory(const MemoryData& value) {
        map->UpdateMemory(Address, reinterpret_cast<const uint8*>(&value), sizeof(MemoryData));
        if(onUpdate) {
            onUpdate(value);
        }
    }

    template <typename MemoryData = RawDataType>
    inline const MemoryData* GetMemory() const {
        return reinterpret_cast<const MemoryData*>(map->GetMemory(Address));
    }
};



template <uint32 Address, typename DataType, typename RawDataType>
class RegisterDataMutator : public RegisterData<Address, RawDataType> {
protected:
    std::function<DataType(const RawDataType&)> readMutator;
    std::function<RawDataType(const DataType&)> writeMutator;

public:
    RegisterDataMutator(
        std::function<DataType(const RawDataType&)> readMut = nullptr,
        std::function<RawDataType(const DataType&)> writeMut = nullptr,
        std::function<void(const RawDataType&)> event = nullptr
    ) :
        RegisterData<Address, RawDataType>(std::move(event)),
        readMutator(std::move(readMut)),
        writeMutator(std::move(writeMut)) 
    {
    }

    DataType Get() const {
        const auto* storageRef = this->GetMemory();
        return readMutator ? readMutator(*storageRef) : static_cast<DataType>(*storageRef);
    }

    void Set(const DataType& value) {
        RawDataType transformedValue = writeMutator ? writeMutator(value) : static_cast<RawDataType>(value);
        this->UpdateMemory(transformedValue);
    }

    inline RegisterDataMutator& operator=(const DataType& value) {
        Set(value);
        return *this;
    }

    inline operator DataType() const {
        return Get();
    }
};



template <uint32 Address, typename DataType = int16_t, uint32 Accuracy = 100>
class RegisterDataFloat : public RegisterDataMutator<Address, float, DataType> {
public:
    RegisterDataFloat(std::function<void(const float&)> event = nullptr)
        : RegisterDataMutator<Address, float, DataType>(&ReadMutator, &WriteMutator, std::move(event)) {}

    inline RegisterDataFloat& operator=(const float& value) {
        this->Set(value);
        return *this;
    }

    inline operator float() const {
        return this->Get();
    }
private:
    static float ReadMutator(const DataType& rawValue) {
        return static_cast<float>(rawValue) / static_cast<float>(Accuracy);
    }

    static DataType WriteMutator(const float& value) {
        return static_cast<DataType>(value * static_cast<float>(Accuracy));
    }
};