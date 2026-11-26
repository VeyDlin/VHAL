#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/CompileTime/Require.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>


enum class SpiBitOrder : uint8 {
	MsbFirst,
	LsbFirst
};


// The framing of a register access on SPI. Each access sends one command byte, then the data, in one chip-select window.
// The command byte holds the register address in its low addressBits bits and the read flag at readBit. The bit order sets the
// order in which the command byte's bits go out; the data bytes go out as the device sends them.
//
// Defaults: read bit 7, 7 address bits, most significant bit first. The read bit must be at or above the address bits.
//
//   SpiFramingBuilder().ReadBit(7).AddressBits(6)     // for a device with a multi-byte flag at bit 6
struct SpiFramingBuilder {
	uint8 readBit = 7;
	uint8 addressBits = 7;
	SpiBitOrder bitOrder = SpiBitOrder::MsbFirst;


	constexpr SpiFramingBuilder ReadBit(uint8 position) const {
		Require<"RegisterBus: the read bit is 0 to 7">(position <= 7);
		SpiFramingBuilder result = *this;
		result.readBit = position;
		return result;
	}


	constexpr SpiFramingBuilder AddressBits(uint8 count) const {
		Require<"RegisterBus: address bits are 1 to 7">(count >= 1 && count <= 7);
		SpiFramingBuilder result = *this;
		result.addressBits = count;
		return result;
	}


	constexpr SpiFramingBuilder BitOrder(SpiBitOrder order) const {
		SpiFramingBuilder result = *this;
		result.bitOrder = order;
		return result;
	}


	// The rule between the fields: the read bit does not sit inside the address bits. Called by SpiRegisterBus.
	constexpr bool Valid() const {
		Require<"RegisterBus: the read bit must not be one of the address bits">(readBit >= addressBits);
		return true;
	}
};


constexpr SpiFramingBuilder SpiDefaultFraming() {
	return SpiFramingBuilder();
}


// The register bus over an SPI adapter. Adapter is an SPIAdapter<...> of the port, or any class with Continuous, WriteArray and
// ReadArray. One bus object serves one chip, because one adapter has one chip-select pin. Make is the framing, a captureless
// lambda that returns a SpiFramingBuilder; the default is SpiDefaultFraming.
//
//   using Bus = SpiRegisterBus<SPIAdapterG4, +[] { return SpiFramingBuilder().ReadBit(7).AddressBits(7); }>;
//   Bus bus(spi);
//
// The descriptor's address must fit the address bits, and its address width must be 1 byte: otherwise the access returns
// invalidParameter (a larger address) or notSupported (a 2-byte width), and nothing is sent. The device address is ignored.
template<typename Adapter, auto (*Make)() = &SpiDefaultFraming>
class SpiRegisterBus {
private:
	static constexpr SpiFramingBuilder framing = Make();
	static constexpr bool framingChecked = framing.Valid();


	static_assert(framingChecked, "the SPI framing was not checked");


	Adapter& adapter;


public:
	explicit SpiRegisterBus(Adapter& spi) : adapter(spi) {
	}


	ResultStatus ReadRegs(RegisterRef where, std::span<uint8> in) {
		uint8 command = 0;
		ResultStatus status = BuildCommand(where, true, command);
		if (status != ResultStatus::ok) {
			return status;
		}

		adapter.Continuous(true);
		status = adapter.WriteArray(&command, 1);
		if (status == ResultStatus::ok) {
			status = adapter.ReadArray(in.data(), static_cast<uint32>(in.size()));
		}
		adapter.Continuous(false);
		return status;
	}


	ResultStatus WriteRegs(RegisterRef where, std::span<const uint8> out) {
		uint8 command = 0;
		ResultStatus status = BuildCommand(where, false, command);
		if (status != ResultStatus::ok) {
			return status;
		}

		adapter.Continuous(true);
		status = adapter.WriteArray(&command, 1);
		if (status == ResultStatus::ok) {
			status = adapter.WriteArray(out.data(), static_cast<uint32>(out.size()));
		}
		adapter.Continuous(false);
		return status;
	}


private:
	static uint8 Reverse(uint8 value) {
		uint8 result = 0;
		for (uint8 index = 0; index < 8; index++) {
			result = static_cast<uint8>((result << 1) | ((value >> index) & 1));
		}
		return result;
	}


	static ResultStatus BuildCommand(RegisterRef where, bool read, uint8& command) {
		if (where.addressWidth != 1) {
			return ResultStatus::notSupported;
		}

		uint32 addressLimit = 1u << framing.addressBits;
		if (where.address >= addressLimit) {
			return ResultStatus::invalidParameter;
		}

		uint8 value = static_cast<uint8>(where.address);
		if (read) {
			value = static_cast<uint8>(value | (1u << framing.readBit));
		}
		if (framing.bitOrder == SpiBitOrder::LsbFirst) {
			value = Reverse(value);
		}

		command = value;
		return ResultStatus::ok;
	}
};
