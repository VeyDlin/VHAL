#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <concepts>
#include <span>


// The table of a Modbus register. I2C and SPI ignore it.
enum class RegisterTable : uint8 {
	Holding,
	Input,
	Coil,
	Discrete
};


// Where a register is on a bus. Each bus uses the fields it needs: I2C the device and the address width, Modbus the table and the
// address, SPI the address. The bytes move in the order the device sends them; the free templates apply the byte order.
struct RegisterRef {
	uint8 device;
	uint16 address;
	uint8 addressWidth;
	RegisterTable table;
};


// A bus that moves register bytes. ReadRegs fills the span with the bytes of the register; WriteRegs sends the span.
template<typename Bus>
concept RegisterBus = requires(Bus& bus, RegisterRef where, std::span<uint8> in, std::span<const uint8> out) {
	{ bus.ReadRegs(where, in) } -> std::same_as<ResultStatus>;
	{ bus.WriteRegs(where, out) } -> std::same_as<ResultStatus>;
};
