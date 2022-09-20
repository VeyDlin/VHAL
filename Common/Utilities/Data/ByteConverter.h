#pragma once
#include <Utilities/DataTypes.h>
#include <utility>
#include <cassert>



class ByteConverter {
public:
	template <typename TO_ARRAY, typename FROM_DATA>
	static void ToArrayFromDataCopy(TO_ARRAY &array, FROM_DATA &copyData, uint16 startToCopyPosition = 0) {
		memcpy(&array + startToCopyPosition, &copyData, sizeof(FROM_DATA));
	}


	template <typename TO_DATA, typename FROM_ARRAY>
	static void ToDataFromArrayCopy(TO_DATA &data, FROM_ARRAY &copyArray, uint16 startToCopyPosition = 0) {
		memcpy(&data, copyArray + startToCopyPosition, sizeof(TO_DATA));
	}
	
	
	template <typename TO_DATA, typename FROM_ARRAY>
	static TO_DATA FromArray(FROM_ARRAY &copyArray, uint16 startToCopyPosition = 0) {
		TO_DATA data;
		ToDataFromArrayCopy(data, copyArray, startToCopyPosition);
		return data;
	}





	template <typename Type>
	static Type GetType(uint8* data, bool reverse = false) {
		Type val;
		memcpy(&val, data, sizeof(Type));
		if(reverse) {
			ReverseArray(val);
		}
		return val;
	}





	template<typename dataType>
	static void ReverseArray(dataType& data) {
		uint8* first = (uint8*)&data;
		uint8* last = &first[sizeof(dataType)];
		while((first != last) && (first != --last)) {
			std::swap(*first++, *last);
		}
	}





	template<typename dataType>
	static inline uint8 GetByte(dataType& data, uint8 position) {
		SystemAssert(sizeof(dataType) > position);
		return static_cast<uint8>((data >> (8 * position)) & 0xFF);
	}





	template<typename dataType>
	static void SetByte(dataType& data, uint8 val, uint8 position) {
		SystemAssert(sizeof(dataType) > position);
		*(static_cast<uint8*>(data) + position) = val;
	}





	static inline uint8 GetHigh(uint16 val) {
		return static_cast<uint8>(val >> 8);
	}

	static inline uint16 GetHigh(uint32 val) {
		return static_cast<uint16>(val >> 16);
	}





	static inline uint8 GetLow(uint16 val) {
		return static_cast<uint8>(val & 0xFF);
	}
	static inline uint16 GetLow(uint32 val) {
		return static_cast<uint16>(val & 0xFFFF);
	}





	static inline uint16 Create(uint8 high, uint8 low) {
	    return static_cast<uint16>((high << 8) | low);
	}

	static inline uint32 Create(uint16 high, uint16 low) {
	    return static_cast<uint32>((high << 16) | low);
	}





	static inline void SetLow(uint16 &to, uint8 set) {
		to &= 0xFF << 8;
		to |= set;
	}

	static inline void SetLow(uint32 &to, uint16 set) {
		 to &= 0xFF << 16;
		 to |= set;
	}





	static inline void SetHigh(uint16 &to, uint8 set) {
		to &= 0xFF;
		to |= static_cast<uint16>(set << 8);
	}

	static inline void SetHigh(uint32 &to, uint16 set) {
		to &= 0xFFFF;
		to |= static_cast<uint32>(set << 16);
	}

};






