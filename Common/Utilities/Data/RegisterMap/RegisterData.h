#pragma once
#include <VHAL.h>
#include <utility>
#include <functional>
#include <limits>
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

	inline bool CanOnWrite() const override {
        return onWrite != nullptr;
    }

	inline bool CanOnRead() const override {
        return onRead != nullptr;
    }

	inline size_t DataTypeSize() const override {
        return sizeof(RawDataType);
    }

	inline uint32 Addres() const override {
        return Address;
    }

    static constexpr size_t GetDataTypeSize() {
        return sizeof(RawDataType);
    }

    static constexpr uint32 GetAddress() {
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

        return onWrite(data);
    }

	size_t GetUnsafe(uint8* outData) const override {
    	auto dataMemory = map->GetMemoryUnsafe(Address);
    	if (dataMemory == nullptr) {
    		return 0;
    	}

    	if(onRead) {
            alignas(RawDataType) RawDataType tempData;
            std::memcpy(&tempData, dataMemory, sizeof(RawDataType));

            auto readData = onRead(tempData);

            auto data = reinterpret_cast<const uint8*>(&readData);
            std::memcpy(outData, data, DataTypeSize());
    	} else {
    		std::memcpy(outData, dataMemory, DataTypeSize());
    	}

    	return DataTypeSize();
   }


protected:
    template <typename MemoryData = RawDataType>
    inline bool UpdateMemory(const MemoryData& value) {
        return map->UpdateMemory(Address, reinterpret_cast<const uint8*>(&value), sizeof(MemoryData));
    }

    template <typename MemoryData>
    const MemoryData GetData() const {
    	MemoryData data;

    	System::CriticalSection(true);
    	GetUnsafe(reinterpret_cast<uint8*>(&data));
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
		std::function<bool(const RawDataType&)> event = nullptr
	) :
		readMutator(std::move(readMut)),
		writeMutator(std::move(writeMut)) {
		this->onWrite = std::move(event);
	}

	DataType Get() const {
		auto data = this->template GetData<RawDataType>();
		return readMutator ? readMutator(data) : static_cast<DataType>(data);
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
	RegisterDataFloat(std::function<bool(const DataType&)> event = nullptr)
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

	// Round half away from zero, then saturate to the raw type's range
	static DataType WriteMutator(const float& value) {
		float scaled = value * static_cast<float>(Accuracy);
		float rounded = scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f;
		if (rounded <= static_cast<float>(std::numeric_limits<DataType>::min())) {
			return std::numeric_limits<DataType>::min();
		}
		if (rounded >= static_cast<float>(std::numeric_limits<DataType>::max())) {
			return std::numeric_limits<DataType>::max();
		}
		return static_cast<DataType>(rounded);
	}
};
