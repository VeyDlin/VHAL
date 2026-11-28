#pragma once
#include <Utilities/DataTypes.h>
#include <System/System.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>


// The byte order of a multi-byte value, in memory or on a wire. It is the one byte order of VHAL.
enum class ByteOrder : uint8 {
	little,
	big
};


class ByteConverter {
public:
	// Copy data from FROM_DATA to TO_ARRAY starting at startToCopyPosition
	template <typename TO_ARRAY, typename FROM_DATA>
	static void ToArrayFromDataCopy(TO_ARRAY &array, FROM_DATA &copyData, uint16 startToCopyPosition = 0) {
		memcpy(reinterpret_cast<uint8*>(&array) + startToCopyPosition, &copyData, sizeof(FROM_DATA));
	}


	// Copy data from FROM_ARRAY to TO_DATA starting at startToCopyPosition
	template <typename TO_DATA, typename FROM_ARRAY>
	static void ToDataFromArrayCopy(TO_DATA &data, FROM_ARRAY &copyArray, uint16 startToCopyPosition = 0) {
		memcpy(&data, copyArray + startToCopyPosition, sizeof(TO_DATA));
	}
	
	
	// Creates an object of type TO_DATA from an array starting at startToCopyPosition
	template <typename TO_DATA, typename FROM_ARRAY>
	static TO_DATA FromArray(FROM_ARRAY &copyArray, uint16 startToCopyPosition = 0) {
		TO_DATA data;
		ToDataFromArrayCopy(data, copyArray, startToCopyPosition);
		return data;
	}


	// Returns a value of type Type from a byte array, optionally reversing it
	template <typename Type>
	static Type GetType(uint8* data, bool reverse = false) {
		Type val;
		memcpy(&val, data, sizeof(Type));
		if(reverse) {
			ReverseArray(val);
		}
		return val;
	}


	// Reverses the byte order of the given data object
	template<typename dataType>
	static void ReverseArray(dataType& data) {
		uint8* first = (uint8*)&data;
		uint8* last = &first[sizeof(dataType)];
		while((first != last) && (first != --last)) {
			std::swap(*first++, *last);
		}
	}


	// Returns a byte from the data object at the specified position (0 is the least significant byte)
	template<typename dataType>
	static inline uint8 GetByte(dataType& data, uint8 bytePosition) {
		SystemAssert(sizeof(dataType) > bytePosition);
		using Unsigned = std::make_unsigned_t<dataType>;
		return static_cast<uint8>((static_cast<Unsigned>(data) >> (8 * bytePosition)) & 0xFF);
	}


	// Sets a byte in the data object at the specified position (0 is the least significant byte).
	// Shifts instead of writing memory, so the result does not depend on host byte order.
	template<typename dataType>
	static void SetByte(dataType& data, uint8 val, uint8 bytePosition) {
		SystemAssert(sizeof(dataType) > bytePosition);
		using Unsigned = std::make_unsigned_t<dataType>;
		Unsigned mask = static_cast<Unsigned>(static_cast<Unsigned>(0xFF) << (8 * bytePosition));
		Unsigned shifted = static_cast<Unsigned>(static_cast<Unsigned>(val) << (8 * bytePosition));
		data = static_cast<dataType>((static_cast<Unsigned>(data) & ~mask) | shifted);
	}


	// Returns the high byte of a uint16
	static inline uint8 GetHigh(uint16 val) {
		return static_cast<uint8>(val >> 8);
	}

	// Returns the high 16 bits of a uint32
	static inline uint16 GetHigh(uint32 val) {
		return static_cast<uint16>(val >> 16);
	}


	// Returns the low byte of a uint16
	static inline uint8 GetLow(uint16 val) {
		return static_cast<uint8>(val & 0xFF);
	}

	// Returns the low 16 bits of a uint32
	static inline uint16 GetLow(uint32 val) {
		return static_cast<uint16>(val & 0xFFFF);
	}


	// Creates a uint16 from high and low bytes
	static inline uint16 Create(uint8 high, uint8 low) {
	    return static_cast<uint16>((high << 8) | low);
	}

	// Creates a uint32 from high and low 16-bit values
	static inline uint32 Create(uint16 high, uint16 low) {
	    return static_cast<uint32>((high << 16) | low);
	}


	// Sets the low byte of a uint16
	static inline void SetLow(uint16 &to, uint8 set) {
		to &= 0xFF << 8;
		to |= set;
	}

	// Sets the low 16 bits of a uint32
	static inline void SetLow(uint32 &to, uint16 set) {
		 to &= 0xFFFF0000u;
		 to |= set;
	}


	// Sets the high byte of a uint16
	static inline void SetHigh(uint16 &to, uint8 set) {
		to &= 0xFF;
		to |= static_cast<uint16>(set << 8);
	}

	// Sets the high 16 bits of a uint32
	static inline void SetHigh(uint32 &to, uint16 set) {
		to &= 0x0000FFFF;
		to |= static_cast<uint32>(set << 16);
	}


	// Writes value at bytes[offset] in the given byte order. T is an integer of 1 to 8 bytes or an enum (by its underlying type).
	// The span form has no run-time bounds check: the caller checks the range. An access past the end of a std::array in a constant
	// expression does not compile.
	template<typename T, ByteOrder order>
	static constexpr void Put(std::span<uint8> bytes, std::size_t offset, T value) {
		Put<T, order>(bytes.data(), offset, value);
	}


	// The pointer form has no bounds check at all: the caller owns the range.
	template<typename T, ByteOrder order>
	static constexpr void Put(uint8* bytes, std::size_t offset, T value) {
		ValueCheck<T>();
		PutBytes(bytes, offset, Widen(value), sizeof(T), order);
	}


	// Writes the low width bytes of value (width 1 to 8, a compile-time constant).
	template<std::size_t width, ByteOrder order>
	static constexpr void Put(std::span<uint8> bytes, std::size_t offset, uint64 value) {
		Put<width, order>(bytes.data(), offset, value);
	}


	template<std::size_t width, ByteOrder order>
	static constexpr void Put(uint8* bytes, std::size_t offset, uint64 value) {
		static_assert(width >= 1 && width <= 8, "ByteConverter: a width is 1 to 8 bytes");
		PutBytes(bytes, offset, value, width, order);
	}


	// Writes the low width bytes of value, with a width of 1 to 8 given at run time. A width outside that range is not checked.
	template<ByteOrder order>
	static constexpr void Put(std::span<uint8> bytes, std::size_t offset, uint64 value, std::size_t width) {
		PutBytes(bytes.data(), offset, value, width, order);
	}


	template<ByteOrder order>
	static constexpr void Put(uint8* bytes, std::size_t offset, uint64 value, std::size_t width) {
		PutBytes(bytes, offset, value, width, order);
	}


	// Reads a value of type T from bytes[offset] in the given byte order. The same rules as Put apply.
	template<typename T, ByteOrder order>
	static constexpr T Get(std::span<const uint8> bytes, std::size_t offset) {
		return Get<T, order>(bytes.data(), offset);
	}


	template<typename T, ByteOrder order>
	static constexpr T Get(const uint8* bytes, std::size_t offset) {
		ValueCheck<T>();
		return FromWide<T>(GetBytes(bytes, offset, sizeof(T), order));
	}


	// Reads width bytes (width 1 to 8, a compile-time constant) as a uint64.
	template<std::size_t width, ByteOrder order>
	static constexpr uint64 Get(std::span<const uint8> bytes, std::size_t offset) {
		return Get<width, order>(bytes.data(), offset);
	}


	template<std::size_t width, ByteOrder order>
	static constexpr uint64 Get(const uint8* bytes, std::size_t offset) {
		static_assert(width >= 1 && width <= 8, "ByteConverter: a width is 1 to 8 bytes");
		return GetBytes(bytes, offset, width, order);
	}


	// Reads width bytes, with a width of 1 to 8 given at run time, as a uint64. A width outside that range is not checked.
	template<ByteOrder order>
	static constexpr uint64 Get(std::span<const uint8> bytes, std::size_t offset, std::size_t width) {
		return GetBytes(bytes.data(), offset, width, order);
	}


	template<ByteOrder order>
	static constexpr uint64 Get(const uint8* bytes, std::size_t offset, std::size_t width) {
		return GetBytes(bytes, offset, width, order);
	}


	// Encodes a value of type T as sizeof(T) bytes in the given byte order.
	template<typename T, ByteOrder order>
	static constexpr std::array<uint8, sizeof(T)> ToBytes(T value) {
		ValueCheck<T>();
		std::array<uint8, sizeof(T)> result{};
		PutBytes(result.data(), 0, Widen(value), sizeof(T), order);
		return result;
	}


	// Decodes the first sizeof(T) bytes of an array or span in the given byte order. The span is not checked for its size.
	template<typename T, ByteOrder order>
	static constexpr T FromBytes(std::span<const uint8> bytes) {
		ValueCheck<T>();
		return FromWide<T>(GetBytes(bytes.data(), 0, sizeof(T), order));
	}


private:
	// Compile-time checks of a value type. bool and floating-point types have no byte order here.
	template<typename T>
	static constexpr void ValueCheck() {
		if constexpr (std::is_same_v<T, bool>) {
			static_assert(sizeof(T) == 0, "ByteConverter: bool has no byte width; convert it to an integer type first");
		} else if constexpr (std::is_floating_point_v<T>) {
			static_assert(sizeof(T) == 0, "ByteConverter: floating-point values are not supported; convert them to an integer first");
		} else {
			static_assert(std::is_integral_v<T> || std::is_enum_v<T>, "ByteConverter: the value type is an integer or an enum");
			static_assert(sizeof(T) <= 8, "ByteConverter: the value type is 8 bytes or less");
		}
	}


	// The value as 64 bits, for the byte shifts. An enum is widened from its underlying type.
	template<typename T>
	static constexpr uint64 Widen(T value) {
		if constexpr (std::is_enum_v<T>) {
			return static_cast<uint64>(static_cast<std::underlying_type_t<T>>(value));
		} else {
			return static_cast<uint64>(value);
		}
	}


	// The inverse of Widen. The conversion of a larger unsigned value to a signed type wraps, which C++23 defines.
	template<typename T>
	static constexpr T FromWide(uint64 raw) {
		if constexpr (std::is_enum_v<T>) {
			return static_cast<T>(static_cast<std::underlying_type_t<T>>(raw));
		} else {
			return static_cast<T>(raw);
		}
	}


	// The byte shifts are not templates: one copy serves every value type and width.
	static constexpr void PutBytes(uint8* bytes, std::size_t offset, uint64 value, std::size_t width, ByteOrder order) {
		for (std::size_t index = 0; index < width; index++) {
			std::size_t position = order == ByteOrder::little ? index : width - 1 - index;
			bytes[offset + position] = static_cast<uint8>(value >> (8 * index));
		}
	}


	static constexpr uint64 GetBytes(const uint8* bytes, std::size_t offset, std::size_t width, ByteOrder order) {
		uint64 value = 0;
		for (std::size_t index = 0; index < width; index++) {
			std::size_t position = order == ByteOrder::little ? index : width - 1 - index;
			value |= static_cast<uint64>(bytes[offset + position]) << (8 * index);
		}
		return value;
	}
};
