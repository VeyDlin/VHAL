#pragma once
#include <System/System.h>



class Bit {
public:
	template <typename T, class...T1>
	static constexpr T ByteSet(T1...bits) {
		T i = 0;
		using expand = int[];
		void(expand{ 0, ((i |= (T(1) << bits)), 0)... });
		return i;
	}

	template<typename T, typename T1>
	static inline void Set(T &value, T1 bit) {
		SystemAssert((sizeof(T) * 8U) > bit);
		value |= static_cast<T>(static_cast<T>(1) << static_cast<T>(bit));
	}


	template<typename T, typename T1>
	static inline void Clear(T &value, T1 bit) {
		SystemAssert((sizeof(T) * 8U) > bit);
		value &= ~static_cast<T>(static_cast<T>(1) << static_cast<T>(bit));
	}


	template<typename T, typename T1>
	static inline void Toggle(T &value, T1 bit) {
		SystemAssert((sizeof(T) * 8U) > bit);
		value ^= static_cast<T>(static_cast<T>(1) << static_cast<T>(bit));
	}


	template<typename T, typename T1>
	static inline bool Check(const T &value, T1 bit) {
		SystemAssert((sizeof(T) * 8U) > bit);
		return !((value & (static_cast<T>(1) << static_cast<T>(bit))) == static_cast<T>(0U));
	}


	template<typename T, typename T1>
	static inline void SetValue(T &value, T1 bit) {
		SystemAssert((sizeof(T) * 8U) > bit);
		value = static_cast<T>(static_cast<T>(1) << static_cast<T>(bit));
	}


	template<typename T, typename T1, typename T2>
	static inline void SetAt(T &value, T1 bits, T2 position) {
		SystemAssert((sizeof(T) * 8U) > position);
		value |= (static_cast<T>(static_cast<T>(bits) << static_cast<T>(position)));
	}


	template<typename T, typename T1, typename T2>
	static inline void ClearAt(T &value, T1 bits, T2 position) {
		SystemAssert((sizeof(T) * 8U) > position);
		value ^= ~(static_cast<T>(static_cast<T>(bits) << static_cast<T>(position)));
	}


	template<typename T, typename T1>
	static inline void Write(T &value, T1 position, bool bit) {
		if(bit) {
			Set(value, position);
		} else {
			Clear(value, position);
		}
	}
};
