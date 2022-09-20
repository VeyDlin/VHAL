#pragma once
#include <DataTypes.h>



class COBS {
public:
	static size_t Encode(uint8 *buffer, const size_t size, const size_t offset) {
		if (size > 254) {
			return 0;
		}

		uint8 *startOfData = buffer + offset;
		uint8 *endOfBlock = &startOfData[size - 1];
		uint8 *cursor;

		*(startOfData - 1) = 0x00;

		do {
			for (cursor = endOfBlock; *cursor != 0x00; cursor--)
				;
			*cursor = endOfBlock - cursor + 1;
			endOfBlock = cursor - 1;
		} while (cursor > startOfData);

		if (*(startOfData - 1) == 0x00) {
			*(startOfData - 1) = 0x01;
		}
		return size + 1;
	}




	static size_t Decode(uint8 *buffer, const size_t size) {
		if (size < 1 || size > 255) {
			return 0;
		}

		uint8 tmp = 0;
		uint8 *endOfBuffer = buffer + size;

		do {
			tmp = *buffer;
			*buffer = 0x00;
			buffer += tmp;
		} while (buffer < endOfBuffer);

		return size - 1;
	}
};

