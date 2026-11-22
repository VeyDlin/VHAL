#pragma once
#include <Drivers/Interface/Modbus/ModbusResult.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// A register bus over a Modbus client. The register's table picks the function code: holding registers use 03 to read, and 06 for
// one register or 10 for several to write; input registers use 04; coils use 01 to read, and 05 to write one coil; discrete
// inputs use 02. The device field and the address width are ignored, and the slave address is the client's.
//
// The bytes are the register's bytes in the order the device sends them, so the free templates apply the byte order. A word table
// takes an even number of bytes, one word each, most significant byte first. A coil table takes one byte, the coil in bit 0.
//
// Client is a ModbusClient type. The bus holds a pointer to it; the client must outlive the bus.
template<typename Client>
class ModbusRegisterBus {
private:
	Client* owner;


public:
	explicit ModbusRegisterBus(Client& client) : owner(&client) {
	}


	ResultStatus ReadRegs(RegisterRef where, std::span<uint8> in) {
		if (where.table == RegisterTable::Coil || where.table == RegisterTable::Discrete) {
			return ReadBits(where, in);
		}

		return ReadWords(where, in);
	}


	ResultStatus WriteRegs(RegisterRef where, std::span<const uint8> out) {
		if (where.table == RegisterTable::Input || where.table == RegisterTable::Discrete) {
			return ResultStatus::notSupported;
		}

		if (where.table == RegisterTable::Coil) {
			return WriteCoil(where, out);
		}

		return WriteWords(where, out);
	}


private:
	// Reads holding or input registers into the bytes of in.
	ResultStatus ReadWords(RegisterRef where, std::span<uint8> in) {
		if (in.empty() || in.size() % 2 != 0 || in.size() / 2 > 125) {
			return ResultStatus::invalidParameter;
		}

		std::size_t quantity = in.size() / 2;
		std::array<uint16, 125> words{};
		ModbusResult result = where.table == RegisterTable::Holding
			? owner->ReadHoldingRegisters(where.address, std::span<uint16>(words.data(), quantity))
			: owner->ReadInputRegisters(where.address, std::span<uint16>(words.data(), quantity));

		if (result.status != ResultStatus::ok) {
			return result.status;
		}

		for (std::size_t index = 0; index < quantity; index++) {
			in[2 * index] = static_cast<uint8>(words[index] >> 8);
			in[2 * index + 1] = static_cast<uint8>(words[index] & 0xFF);
		}

		return ResultStatus::ok;
	}


	// Reads coils or discrete inputs: one byte, the bit in bit 0.
	ResultStatus ReadBits(RegisterRef where, std::span<uint8> in) {
		if (in.size() != 1) {
			return ResultStatus::invalidParameter;
		}

		std::array<bool, 1> bits{};
		ModbusResult result = where.table == RegisterTable::Coil
			? owner->ReadCoils(where.address, std::span<bool>(bits))
			: owner->ReadDiscreteInputs(where.address, std::span<bool>(bits));

		if (result.status != ResultStatus::ok) {
			return result.status;
		}

		in[0] = bits[0] ? 1 : 0;
		return ResultStatus::ok;
	}


	// Writes holding registers: 06 for one register, 10 for more.
	ResultStatus WriteWords(RegisterRef where, std::span<const uint8> out) {
		if (out.empty() || out.size() % 2 != 0 || out.size() / 2 > 123) {
			return ResultStatus::invalidParameter;
		}

		std::size_t quantity = out.size() / 2;
		std::array<uint16, 123> words{};
		for (std::size_t index = 0; index < quantity; index++) {
			words[index] = static_cast<uint16>((static_cast<uint16>(out[2 * index]) << 8) | out[2 * index + 1]);
		}

		ModbusResult result = quantity == 1
			? owner->WriteSingleRegister(where.address, words[0])
			: owner->WriteMultipleRegisters(where.address, std::span<const uint16>(words.data(), quantity));

		return result.status;
	}


	// Writes one coil (05) from bit 0 of the byte.
	ResultStatus WriteCoil(RegisterRef where, std::span<const uint8> out) {
		if (out.size() != 1) {
			return ResultStatus::invalidParameter;
		}

		ModbusResult result = owner->WriteSingleCoil(where.address, (out[0] & 1) != 0);
		return result.status;
	}
};
