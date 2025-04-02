#pragma once
#include <System/System.h>
#include <utility>
#include <functional>
#include "IRegisterData.h"


template <uint32 Address, typename RawDataType>
class RegisterData : public IRegisterData {
public:
    std::function<bool(const RawDataType&)> onWrite;
    std::function<RawDataType(const RawDataType&)> onRead;

public:
    inline RegisterData& SetEvents(
    	std::function<bool(const RawDataType&)> writeEvent = nullptr,
		std::function<RawDataType(const RawDataType&)> readEvent = nullptr
	) {
        onWrite = writeEvent;
        onRead = readEvent;
        return *this;
    }

    inline RegisterData& SetAccess(bool writeAccess = true, bool readAccess = true) {
    	write = writeAccess;
    	read = readAccess;
        return *this;
    }

    inline RegisterData& ReadOnly() {
    	write = false;
    	read = true;
        return *this;
    }

    inline RegisterData& WriteOnly() {
    	write = true;
    	read = false;
        return *this;
    }

    inline bool CanOnWrite() override {
        return onWrite != nullptr;
    }

    inline bool CanOnRead() override {
        return onRead != nullptr;
    }

    inline size_t DataTypeSize() override {
        return sizeof(RawDataType);
    }

    inline uint32 Addres() override {
        return Address;
    }

    static constexpr size_t GetDataTypeSize() noexcept {
        return sizeof(RawDataType);
    }

    static constexpr uint32 GetAddress() noexcept {
        return Address;
    }

    inline RawDataType Get() const {
        return GetData<RawDataType>();
    }

    inline bool Set(const RawDataType& value) {
        return UpdateMemory(value);
    }

    inline RegisterData& operator=(const RawDataType& value) {
        Set(value);
        return *this;
    }

    inline operator RawDataType() const {
        return Get();
    }

    bool WriteEvent(const uint8* buffer) override {
        if(!onWrite) {
            return true;

        }
        RawDataType data;
        std::memcpy(&data, buffer, DataTypeSize());

        System::CriticalSection(true);
        auto writeData = onWrite(data);
        System::CriticalSection(false);

        return writeData;
    }

protected:
    template <typename MemoryData = RawDataType>
    inline bool UpdateMemory(const MemoryData& value) {
        return map->UpdateMemory(Address, reinterpret_cast<const uint8*>(&value), sizeof(MemoryData));
    }

    template <typename MemoryData>
    const MemoryData GetData() const {
        if(onRead) {
            System::CriticalSection(true);
            auto readData = onRead(*reinterpret_cast<const RawDataType*>(map->GetMemoryUnsafe(Address)));
            System::CriticalSection(false);

            return *reinterpret_cast<const MemoryData*>(&readData);
        }

        MemoryData data;

		System::CriticalSection(true);
        data = *reinterpret_cast<const MemoryData*>(map->GetMemoryUnsafe(Address));
        System::CriticalSection(false);

        return data;
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
        writeMutator(std::move(writeMut)) {
    }

    DataType Get() const {
        auto data = this->template GetData<RawDataType>();
        return readMutator ? readMutator(data) : *static_cast<DataType*>(&data);
    }

    bool Set(const DataType& value) {
        RawDataType transformedValue = writeMutator ? writeMutator(value) : static_cast<RawDataType>(value);
        return this->UpdateMemory(transformedValue);
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
