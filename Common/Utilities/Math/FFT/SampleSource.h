#pragma once
#include <Utilities/DataTypes.h>
#include <concepts>
#include <type_traits>


// A sample source gives sample i as a Number through source[i], for i from 0 to Size - 1. A pointer or an array of Number is
// a source, and so is any class with the same operator. The FFT reads each sample once, in index order. The sample type must
// be exactly Number: a source of another type, such as a raw uint16 buffer for a float transform, is a compile error instead
// of a silent conversion. Use AdcSamples to convert raw ADC codes.
template <typename Source, typename Number>
concept SampleSourceOf = requires(const Source &source, uint32 index) {
	requires std::same_as<std::remove_cvref_t<decltype(source[index])>, Number>;
};
