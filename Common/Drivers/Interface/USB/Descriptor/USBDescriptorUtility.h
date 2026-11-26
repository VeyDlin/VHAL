#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <System/System.h>
#include <Utilities/CompileTime/Require.h>


// Helpers of the descriptor builders. The builders are constexpr and are meant for constant expressions only (constexpr
// variables or template arguments), where they run at compile time into static storage. A rejected input fails a Require
// check (Utilities/CompileTime/Require.h), and the compiler error shows its message. A run-time call of a builder fails at
// link time whatever its arguments, because the call stays in the code.


constexpr uint16 USBDescriptorRead16(std::span<const uint8> bytes, std::size_t offset) {
	Require<"USB: descriptor field is outside the descriptor">(offset + 2 <= bytes.size());
	return static_cast<uint16>(bytes[offset] | (bytes[offset + 1] << 8));
}


template<std::size_t Size>
constexpr void USBDescriptorPut8(std::array<uint8, Size>& bytes, std::size_t offset, uint8 value) {
	Require<"USB: descriptor field is outside the descriptor">(offset < Size);
	bytes[offset] = value;
}


template<std::size_t Size>
constexpr void USBDescriptorPut16(std::array<uint8, Size>& bytes, std::size_t offset, uint16 value) {
	Require<"USB: descriptor field is outside the descriptor">(offset + 2 <= Size);
	bytes[offset] = static_cast<uint8>(value & 0xFF);
	bytes[offset + 1] = static_cast<uint8>(value >> 8);
}


template<std::size_t Size>
constexpr void USBDescriptorPut32(std::array<uint8, Size>& bytes, std::size_t offset, uint32 value) {
	Require<"USB: descriptor field is outside the descriptor">(offset + 4 <= Size);
	for (std::size_t index = 0; index < 4; index++) {
		bytes[offset + index] = static_cast<uint8>((value >> (8 * index)) & 0xFF);
	}
}


// Copies piece to bytes at offset and returns the offset after it. Builders append their pieces in order, so the pieces
// fill the array exactly when its size is the sum of their sizes.
template<std::size_t Size>
constexpr std::size_t USBDescriptorAppend(std::array<uint8, Size>& bytes, std::size_t offset, std::span<const uint8> piece) {
	Require<"USB: descriptor pieces do not fit the descriptor">(offset + piece.size() <= Size);
	for (std::size_t index = 0; index < piece.size(); index++) {
		bytes[offset + index] = piece[index];
	}
	return offset + piece.size();
}
