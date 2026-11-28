#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Data/ByteConverter.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/CompileTime/Require.h>
#include <cstddef>
#include <type_traits>


enum class RegisterAccess : uint8 {
	Read,
	Write,
	ReadWrite
};


// A register: its address on a bus, its value type and its rules. It owns no storage, so it is a constexpr constant.
//
//   constexpr auto ChargeControl = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();
//
// Each builder method returns a new descriptor. The checks run when the constant is evaluated, so a wrong value is a compile error.
// Defaults: address width 1 byte, big-endian, read and write, Modbus holding table.
template<typename Value>
struct RegisterDescriptor {
	static_assert(std::is_integral_v<Value> && !std::is_same_v<Value, bool>, "a register value type is an integer type");


	using ValueType = Value;

	uint8 device;
	uint16 address;
	uint8 addressWidth = 1;
	ByteOrder byteOrder = ByteOrder::big;
	RegisterAccess access = RegisterAccess::ReadWrite;
	RegisterTable table = RegisterTable::Holding;


	// deviceAddress is the 7-bit I2C address. SPI and Modbus ignore it.
	constexpr RegisterDescriptor(uint8 deviceAddress, uint16 registerAddress) : device(deviceAddress), address(registerAddress) {
		Require<"RegisterMap: device address must be 7 bits">(deviceAddress <= 0x7F);
	}


	constexpr RegisterDescriptor AddressWidth(uint8 width) const {
		Require<"RegisterMap: address width must be 1 or 2 bytes">(width == 1 || width == 2);
		RegisterDescriptor result = *this;
		result.addressWidth = width;
		return result;
	}


	constexpr RegisterDescriptor Order(ByteOrder order) const {
		RegisterDescriptor result = *this;
		result.byteOrder = order;
		return result;
	}


	constexpr RegisterDescriptor Table(RegisterTable kind) const {
		RegisterDescriptor result = *this;
		result.table = kind;
		return result;
	}


	constexpr RegisterDescriptor ReadOnly() const {
		RegisterDescriptor result = *this;
		result.access = RegisterAccess::Read;
		return result;
	}


	constexpr RegisterDescriptor WriteOnly() const {
		RegisterDescriptor result = *this;
		result.access = RegisterAccess::Write;
		return result;
	}


	constexpr RegisterDescriptor ReadWrite() const {
		RegisterDescriptor result = *this;
		result.access = RegisterAccess::ReadWrite;
		return result;
	}


	constexpr bool CanRead() const {
		return access != RegisterAccess::Write;
	}


	constexpr bool CanWrite() const {
		return access != RegisterAccess::Read;
	}


	constexpr std::size_t ByteCount() const {
		return sizeof(Value);
	}


	constexpr RegisterRef Ref() const {
		return RegisterRef{ device, address, addressWidth, table };
	}
};
