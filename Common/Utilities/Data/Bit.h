#pragma once
#include <System/System.h>


class Bit {
public:
	// Sets specified bits in type T
	// Example usage:
	//   auto result = Bit::ByteSet<uint8>(0, 2, 4); // Sets bits 0, 2, and 4 in the value of type uint8
	// Before:  0000 0000
	//            ↑  ↑ ↑
	//            4  2 0 (bit numbers)
	// After:   0001 0101 (bits 0, 2, and 4 are set)
	template <typename T, class... T1>
	static constexpr T ByteSet(T1... bits) {
		T i = 0;
		using expand = int[];
		void(expand{ 0, ((i |= (T(1) << bits)), 0)... });
		return i;
	}

	// Sets a bit in the value
	// Example usage:
	//   uint8 value = 0b0000 0000;
	//   Bit::Set(value, 3); // Sets bit 3
	// Before:  0000 0000
	//              ↑
	//              3    (bit number)
	// After:   0000 1000 (bit 3 is set)
	template<typename T, typename T1>
	static inline void Set(T &value, T1 bit) {
		SystemAssert((sizeof(T) * 8U) > bit);
		value |= static_cast<T>(static_cast<T>(1) << static_cast<T>(bit));
	}

	// Clears a bit in the value
	// Example usage:
	//   uint8 value = 0b1111 1111;
	//   Bit::Clear(value, 2); // Clears bit 2
	// Before:  1111 1111
	//               ↑
	//               2   (bit number)
	// After:    1111 1011 (bit 2 is cleared)
	template<typename T, typename T1>
	static inline void Clear(T &value, T1 bit) {
		SystemAssert((sizeof(T) * 8U) > bit);
		value &= ~static_cast<T>(static_cast<T>(1) << static_cast<T>(bit));
	}

	// Toggles a bit in the value
	// Example usage:
	//   uint8 value = 0b0000 0100;
	//   Bit::Toggle(value, 2); // Toggles bit 2
	// Before:  0000 0100
	//               ↑
	//               2   (bit number)
	// After:    0000 0000 (bit 2 is toggled)
	template<typename T, typename T1>
	static inline void Toggle(T &value, T1 bit) {
		SystemAssert((sizeof(T) * 8U) > bit);
		value ^= static_cast<T>(static_cast<T>(1) << static_cast<T>(bit));
	}

	// Checks if a bit is set in the value
	// Example usage:
	//   uint8 value = 0b0000 0100;
	//   bool isSet = Bit::Check(value, 2); // Checks bit 2
	// Before:  0000 0100
	//               ↑
	//               2 (bit number)
	// Result: true (bit 2 is set)
	template<typename T, typename T1>
	static inline bool Check(const T &value, T1 bit) {
		SystemAssert((sizeof(T) * 8U) > bit);
		return !((value & (static_cast<T>(1) << static_cast<T>(bit))) == static_cast<T>(0U));
	}

	// Sets a bit at a specific position
	// Example usage:
	//   uint8 value;
	//   Bit::SetValue(value, 1); // Sets bit 1 to 1
	// Before:  0000 0000
	//                ↑
	//                1  (bit number)
	// After:   0000 0010 (bit 1 is set)
	template<typename T, typename T1>
	static inline void SetValue(T &value, T1 bit) {
		SystemAssert((sizeof(T) * 8U) > bit);
		value = static_cast<T>(static_cast<T>(1) << static_cast<T>(bit));
	}

	// Sets bits at a specific position
	// Example usage:
	//   uint8 value = 0b0000 0000;
	//   Bit::SetAt(value, 0b11, 4); // Sets bits at position 4
	// Before:  0000 0000
	//            ↑
	//            4      (start position)
	// After:   0011 0000 (bits are set starting from position 4)
	template<typename T, typename T1, typename T2>
	static inline void SetAt(T &value, T1 bits, T2 position) {
		SystemAssert((sizeof(T) * 8U) > position);
		value |= (static_cast<T>(static_cast<T>(bits) << static_cast<T>(position)));
	}

	// Clears bits at a specific position
	// Example usage:
	//   uint8 value = 0b1111 1111;
	//   Bit::ClearAt(value, 0b11, 4); // Clears bits at position 4
	// Before:  1111 1111
	//            ↑
	//            4      (start position)
	// After:   1100 1111 (bits are cleared starting from position 4)
	template<typename T, typename T1, typename T2>
	static inline void ClearAt(T &value, T1 bits, T2 position) {
		SystemAssert((sizeof(T) * 8U) > position);
		value ^= ~(static_cast<T>(static_cast<T>(bits) << static_cast<T>(position)));
	}

	// Writes a bit at a specific position
	// Example usage:
	//   uint8 value = 0b0000 0000;
	//   Bit::Write(value, 3, true); // Writes bit 3 as 1
	// Before:  0000 0000
	//              ↑
	//              3    (bit number)
	// After:   0000 1000 (bit 3 is set)
	template<typename T, typename T1>
	static inline void Write(T &value, T1 position, bool bit) {
		if (bit) {
			Set(value, position);
		} else {
			Clear(value, position);
		}
	}
};