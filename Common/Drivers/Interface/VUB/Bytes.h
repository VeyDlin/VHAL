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
