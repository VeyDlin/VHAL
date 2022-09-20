#pragma once
#include <Utilities/Status.h>
#include <functional>
#include <string>



class Print {
	template<typename T>
	constexpr static bool is_int_v =
			std::is_same<T, int8>::value  || std::is_same<T, uint8>::value	||
			std::is_same<T, int16>::value || std::is_same<T, uint16>::value ||
			std::is_same<T, int32>::value || std::is_same<T, uint32>::value	||
			std::is_same<T, int64>::value || std::is_same<T, uint64>::value;

	template<typename T>
	constexpr static bool is_float_or_double_v =
			std::is_same<T, float>::value || std::is_same<T, double>::value;



public:
	std::function<void(char *string)> writeHandle;

	enum Format {
		Hex = 16,
		Dec = 10,
		Oct = 8,
		Bin = 2
	};


public:
	inline void Line() {
		_Write(const_cast<char*>("\n"), 1);
	}





	inline size_t WriteBytes(uint8 *buffer, size_t size) {
		return CallWriteByteArray(buffer, size);
	}





	inline size_t Write(const char *string) {
		return CallWrite(const_cast<char*>(string));
	}


	inline size_t WriteLn(const char *string) {
		return CallWrite(const_cast<char*>(string), true);
	}





	inline size_t Write(char *string) {
		return CallWrite(string);
	}


	inline size_t WriteLn(char *string) {
		return CallWrite(string, true);
	}





	size_t Write(bool is) {
		return Write(is ? "true" : "false");
	}


	size_t WriteLn(bool is) {
		return WriteLn(is ? "true" : "false");
	}





	template<typename type, class=std::enable_if_t<is_int_v<type>>>
	inline size_t Write(type number, Format format = Format::Dec) {
		return WriteNumber(number, format);
	}


	template<typename type, class=std::enable_if_t<is_int_v<type>>>
	inline size_t WriteLn(type number, Format format = Format::Dec) {
		return WriteNumber(number, format, true);
	}





	template<typename type, class=std::enable_if_t<is_float_or_double_v<type>>>
	inline size_t Write(type number, uint8 accuracy = 2) {
		return WriteFloat(number, accuracy);
	}


	template<typename type, class=std::enable_if_t<is_float_or_double_v<type>>>
	inline size_t WriteLn(type number, uint8 accuracy = 2) {
		return WriteFloat(number, accuracy, true);
	}



private:
	template<typename type, class=std::enable_if_t<is_int_v<type>>>
	size_t WriteNumber(type number, Format format, bool newLine = false) {
		// TODO: Add WriteNumber
		return 0;
	}





	template<typename type, class=std::enable_if_t<is_float_or_double_v<type>>>
	size_t WriteFloat(type number, uint8 digits, bool newLine = false) {
		// TODO: Add WriteFloat
		return 0;
	}





	size_t CallWrite(char *string, bool newLine = false) {
		auto len = strlen(string);
		_Write(string, len);

		if(newLine) {
			len += Write("\r\n");
		}

		return len;
	}



	size_t CallWriteByteArray(uint8 *buffer, size_t size) {
		_Write((char*)buffer, size);
		return size;
	}


protected:
	virtual void _Write(char *string, size_t size) = 0;
};




