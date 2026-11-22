#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Math/IQMath/IQ.h>
#include <type_traits>


// A view over the raw codes of an ADC with Bits of resolution, such as the buffer that a DMA transfer fills. Sample i is the
// code minus the mid code, scaled so that 2^(Bits - 1) codes make 1.0: code 0 gives -1.0, the mid code gives 0, and the top
// code gives 1 - 2^(1 - Bits). The FFT converts each sample while it loads it, so no converted copy exists.
//
// The conversion is exact for float and double, since the scale is a power of two. For an IQ<Q> the offset is scaled in
// integers before the value is formed, so an IQ with a small range takes every code: IQ<24> holds only +-128, but its raw
// value of a 12-bit code is at most 2^24. A Q below Bits - 1 rounds to the nearest raw value, ties toward +infinity, as the
// IQ multiplication does.
template <typename Number, uint32 Bits>
class AdcSamples {
	static_assert(Bits >= 2 && Bits <= 16, "AdcSamples: Bits must be in 2..16");
	static_assert(std::is_floating_point_v<Number> || IQType<Number>, "AdcSamples: Number must be float, double or IQ<Q>");

public:
	// The number of codes that make 1.0: half the code range.
	static constexpr int32 unitCodes = int32(1) << (Bits - 1);
	// The reciprocal of unitCodes, exact in binary, so a float or double multiplies where it would divide.
	static constexpr double inverseUnitCodes = 1.0 / static_cast<double>(unitCodes);


private:
	const uint16 *codes;
	int32 midCode;


public:
	// codes must hold every sample the FFT reads, and each code must be below 2^Bits. The view keeps the pointer, not a copy.
	AdcSamples(const uint16 *codesIn, int32 midCodeIn = unitCodes) : codes(codesIn), midCode(midCodeIn) {}


	Number operator[](uint32 index) const {
		int32 offset = static_cast<int32>(codes[index]) - midCode;
		if constexpr (IQType<Number>) {
			return Number::FromRaw(ScaleRaw(offset));
		} else {
			return static_cast<Number>(offset) * static_cast<Number>(inverseUnitCodes);
		}
	}


private:
	// The raw value of an offset in codes: offset * 2^Q / unitCodes, rounded to the nearest raw value.
	static int32 ScaleRaw(int32 offset) {
		constexpr int32 unitBits = Bits - 1;
		if constexpr (Number::q >= unitBits) {
			return static_cast<int32>(static_cast<int64>(offset) * (int64(1) << (Number::q - unitBits)));
		} else {
			constexpr int32 shift = unitBits - Number::q;
			return static_cast<int32>((static_cast<int64>(offset) + (int64(1) << (shift - 1))) >> shift);
		}
	}
};
