#pragma once
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// Big-endian (network order) field access for the protocol headers. The callers check the bounds first: each function reads or writes
// the bytes from offset on, so a call past the end of the span is a bug in the caller, not a malformed frame.
namespace DemoNetwork {


inline uint16 ReadBe16(std::span<const uint8> bytes, std::size_t offset) {
	return static_cast<uint16>((static_cast<uint32>(bytes[offset]) << 8) | static_cast<uint32>(bytes[offset + 1]));
}


inline uint32 ReadBe32(std::span<const uint8> bytes, std::size_t offset) {
	return (static_cast<uint32>(bytes[offset]) << 24)
		| (static_cast<uint32>(bytes[offset + 1]) << 16)
		| (static_cast<uint32>(bytes[offset + 2]) << 8)
		| static_cast<uint32>(bytes[offset + 3]);
}


inline void WriteBe16(std::span<uint8> bytes, std::size_t offset, uint16 value) {
	bytes[offset] = static_cast<uint8>(value >> 8);
	bytes[offset + 1] = static_cast<uint8>(value & 0xFF);
}


inline void WriteBe32(std::span<uint8> bytes, std::size_t offset, uint32 value) {
	bytes[offset] = static_cast<uint8>(value >> 24);
	bytes[offset + 1] = static_cast<uint8>((value >> 16) & 0xFF);
	bytes[offset + 2] = static_cast<uint8>((value >> 8) & 0xFF);
	bytes[offset + 3] = static_cast<uint8>(value & 0xFF);
}


}
