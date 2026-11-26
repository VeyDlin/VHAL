#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/CompileTime/Require.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>


enum class RegisterAccess : uint8 {
	Read,
	Write,
	ReadWrite
};


enum class RegisterByteOrder : uint8 {
	BigEndian,
	LittleEndian
};


// Packs a value into bytes in the given order. Works in constant expressions, so a register value can be checked at compile time.
template<typename Value>
constexpr std::array<uint8, sizeof(Value)> RegisterToBytes(Value value, RegisterByteOrder order) {
	using Unsigned = std::make_unsigned_t<Value>;
	Unsigned raw = static_cast<Unsigned>(value);
	std::array<uint8, sizeof(Value)> bytes{};
	for (std::size_t index = 0; index < sizeof(Value); index++) {
		std::size_t position = order == RegisterByteOrder::BigEndian ? sizeof(Value) - 1 - index : index;
		bytes[position] = static_cast<uint8>(raw >> (8 * index));
	}
	return bytes;
}


// Unpacks a value from bytes in the given order. Works in constant expressions.
template<typename Value>
constexpr Value RegisterFromBytes(std::span<const uint8> bytes, RegisterByteOrder order) {
	using Unsigned = std::make_unsigned_t<Value>;
	Unsigned raw = 0;
	for (std::size_t index = 0; index < sizeof(Value); index++) {
		std::size_t position = order == RegisterByteOrder::BigEndian ? sizeof(Value) - 1 - index : index;
		raw = static_cast<Unsigned>(raw | (static_cast<Unsigned>(bytes[position]) << (8 * index)));
	}
	return static_cast<Value>(raw);
}


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
	RegisterByteOrder byteOrder = RegisterByteOrder::BigEndian;
	RegisterAccess access = RegisterAccess::ReadWrite;
	RegisterTable table = RegisterTable::Holding;


	// deviceAddress is the 7-bit I2C address. SPI and Modbus ignore it.
	constexpr RegisterDescriptor(uint8 deviceAddress, uint16 registerAddress) : device(deviceAddress), address(registerAddress) {
		Require<"RegisterMap:device address must be 7 bits">(deviceAddress <= 0x7F);
	}


	constexpr RegisterDescriptor AddressWidth(uint8 width) const {
		Require<"RegisterMap:address width must be 1 or 2 bytes">(width == 1 || width == 2);
		RegisterDescriptor result = *this;
		result.addressWidth = width;
		return result;
	}


	constexpr RegisterDescriptor ByteOrder(RegisterByteOrder order) const {
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
