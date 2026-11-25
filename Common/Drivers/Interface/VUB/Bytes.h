#pragma once
#include <Utilities/DataTypes.h>
#include <cstddef>

namespace VUB {


// Copies count bytes. The bytes are copied in rising order, so the two ranges may not overlap unless to is below from.
inline void CopyBytes(const uint8* from, std::size_t count, uint8* to) {
	for (std::size_t index = 0; index < count; index++) {
		to[index] = from[index];
	}
}


// True when the clock has reached due. The difference is taken as a signed 32-bit count of microseconds, so a clock that wraps around 2^32
// still compares right, as long as the two times are less than 2^31 microseconds apart.
inline bool Due(uint32 nowUs, uint32 dueUs) {
	return static_cast<int32>(nowUs - dueUs) >= 0;
}


// The microseconds in a millisecond: the clocks of the layers count in microseconds, and their timeouts are given in milliseconds.
inline constexpr uint32 UsPerMs = 1000;


// The longest timeout in milliseconds. The deadlines are signed 32-bit counts of microseconds, so a timeout above this would wrap.
inline constexpr uint32 MaxTimeoutMs = 2147483;


// The step of the session numbers: the first number of a session is the boot value times this odd factor, so boot values that follow each
// other give first numbers far apart. Each layer keeps its own width and its own use of the value (see RequesterOf::SetSession and
// ReliableWire::FirstOf).
inline constexpr uint32 SessionStep = 0x9E37;


// Little-endian field helpers for header and length fields. They read and write at a byte pointer, so the caller checks the bounds.
// They are plain inline functions: every stack in an image shares the one copy.
inline void PutLe16(uint8* at, uint16 value) {
	at[0] = static_cast<uint8>(value);
	at[1] = static_cast<uint8>(value >> 8);
}


inline uint16 GetLe16(const uint8* at) {
	return static_cast<uint16>(at[0] | (at[1] << 8));
}


inline void PutLe32(uint8* at, uint32 value) {
	at[0] = static_cast<uint8>(value);
	at[1] = static_cast<uint8>(value >> 8);
	at[2] = static_cast<uint8>(value >> 16);
	at[3] = static_cast<uint8>(value >> 24);
}


inline uint32 GetLe32(const uint8* at) {
	return static_cast<uint32>(at[0]) | (static_cast<uint32>(at[1]) << 8) | (static_cast<uint32>(at[2]) << 16) |
		(static_cast<uint32>(at[3]) << 24);
}

}
