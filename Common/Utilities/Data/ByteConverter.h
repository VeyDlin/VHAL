#pragma once
#include <System/System.h>
#include <utility>
#include <cassert>


class ByteConverter {
public:
	// Copy data from FROM_DATA to TO_ARRAY starting at startToCopyPosition
	template <typename TO_ARRAY, typename FROM_DATA>
	static void ToArrayFromDataCopy(TO_ARRAY &array, FROM_DATA &copyData, uint16 startToCopyPosition = 0) {
		memcpy(&array + startToCopyPosition, &copyData, sizeof(FROM_DATA));
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


	// Returns a byte from the data object at the specified position
	template<typename dataType>
	static inline uint8 GetByte(dataType& data, uint8 bytePosition) {
		SystemAssert(sizeof(dataType) > bytePosition);
		return static_cast<uint8>((data >> (8 * bytePosition)) & 0xFF);
	}


	// Sets a byte in the data object at the specified position
	template<typename dataType>
	static void SetByte(dataType& data, uint8 val, uint8 bytePosition) {
		SystemAssert(sizeof(dataType) > bytePosition);
		*(static_cast<uint8*>(data) + bytePosition) = val;
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
		 to &= 0xFF << 16;
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
};
