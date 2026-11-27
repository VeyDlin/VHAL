#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/CompileTime/Require.h>
#include <cstddef>
#include <type_traits>


// The value type of a bit field: bool for one bit, otherwise the smallest unsigned type that holds the width.
template<uint8 Width>
using BitFieldValue = std::conditional_t<Width == 1, bool,
	std::conditional_t<(Width <= 8), uint8,
	std::conditional_t<(Width <= 16), uint16,
	std::conditional_t<(Width <= 32), uint32, uint64>>>>;


// A part of a register, for Modify. Made by BitField<Width>(register, shift), which is the usual way to write it.
//
//   constexpr auto ChargeDisable = BitField<1>(ChargeControl, 3);     // bit 3, a bool
//   Modify<ChargeDisable>(bus, true);
//
// The field must lie inside the register: shift + width at most the register's bit count. Checked when the constant is evaluated.
template<uint8 Width, typename Descriptor>
struct RegisterBitField {
	static_assert(Width >= 1 && Width <= 64, "a bit field is 1 to 64 bits wide");


	using ValueType = BitFieldValue<Width>;
	using RawType = typename Descriptor::ValueType;
	using Unsigned = std::make_unsigned_t<RawType>;

	static constexpr Unsigned ones = Width >= 8 * sizeof(Unsigned) ? static_cast<Unsigned>(~Unsigned(0)) : static_cast<Unsigned>((Unsigned(1) << Width) - 1);

	Descriptor reg;
	uint8 shift;


	constexpr RegisterBitField(Descriptor target, uint8 position) : reg(target), shift(position) {
		Require<"RegisterMap: bit field is outside the register width">(static_cast<std::size_t>(position) + Width <= 8 * sizeof(RawType));
	}


	// True when the value fits the field. A bool always fits.
	constexpr bool Fits(ValueType value) const {
		return static_cast<uint64>(value) <= static_cast<uint64>(ones);
	}


	// The register value with the field replaced by value. The other bits are kept.
	constexpr RawType Insert(RawType raw, ValueType value) const {
		Unsigned mask = static_cast<Unsigned>(ones << shift);
		Unsigned bits = static_cast<Unsigned>(static_cast<Unsigned>(value) << shift);
		return static_cast<RawType>((static_cast<Unsigned>(raw) & static_cast<Unsigned>(~mask)) | (bits & mask));
	}
};


// The field of Width bits at shift in the register. Width is a template argument, because the value type depends on it.
template<uint8 Width, typename Descriptor>
constexpr RegisterBitField<Width, Descriptor> BitField(Descriptor reg, uint8 shift) {
	return RegisterBitField<Width, Descriptor>(reg, shift);
}
