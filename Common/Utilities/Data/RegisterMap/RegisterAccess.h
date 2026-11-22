#pragma once
#include <Utilities/Data/RegisterMap/BitField.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/Data/RegisterMap/RegisterDescriptor.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>


// Free functions for drivers. The register is a template argument, so the access rule is checked at compile time and the
// address is built from the constant.
//
//   Result<uint8> value = Read<ChargeControl>(bus);
//   Write<ChargeControl>(bus, 0x00);
//   Modify<ChargeDisable>(bus, true);      // one read, bit 3 changed, one write


// Reads the register and decodes it in the descriptor's byte order.
template<auto Reg, RegisterBus Bus>
Result<typename decltype(Reg)::ValueType> Read(Bus& bus) {
	static_assert(Reg.CanRead(), "register is write-only: Read<Reg>() needs Read or ReadWrite access");
	using Value = typename decltype(Reg)::ValueType;
	std::array<uint8, sizeof(Value)> bytes{};
	ResultStatus status = bus.ReadRegs(Reg.Ref(), bytes);
	if (status != ResultStatus::ok) {
		return status;
	}
	return RegisterFromBytes<Value>(bytes, Reg.byteOrder);
}


// Encodes the value in the descriptor's byte order and writes the register.
template<auto Reg, RegisterBus Bus>
ResultStatus Write(Bus& bus, typename decltype(Reg)::ValueType value) {
	static_assert(Reg.CanWrite(), "register is read-only: Write<Reg>() needs Write or ReadWrite access");
	using Value = typename decltype(Reg)::ValueType;
	std::array<uint8, sizeof(Value)> bytes = RegisterToBytes<Value>(value, Reg.byteOrder);
	return bus.WriteRegs(Reg.Ref(), bytes);
}


// Reads the register, replaces the field's bits and writes the register back. The other bits keep their values. A value that
// does not fit the field returns invalidParameter before any bus traffic.
template<auto Field, RegisterBus Bus>
ResultStatus Modify(Bus& bus, typename decltype(Field)::ValueType value) {
	static_assert(Field.reg.CanRead() && Field.reg.CanWrite(), "Modify<Field>() needs ReadWrite access");
	using Raw = typename decltype(Field)::RawType;
	if (!Field.Fits(value)) {
		return ResultStatus::invalidParameter;
	}

	std::array<uint8, sizeof(Raw)> bytes{};
	ResultStatus status = bus.ReadRegs(Field.reg.Ref(), bytes);
	if (status != ResultStatus::ok) {
		return status;
	}

	Raw raw = RegisterFromBytes<Raw>(bytes, Field.reg.byteOrder);
	bytes = RegisterToBytes<Raw>(Field.Insert(raw, value), Field.reg.byteOrder);
	return bus.WriteRegs(Field.reg.Ref(), bytes);
}
