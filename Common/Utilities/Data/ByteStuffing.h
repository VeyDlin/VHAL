#pragma once
#include <DataTypes.h>


class ByteStuffing {
public:
	bool CheckSequence(uint8 *buffer, uint16 bufferCheckPoint, uint16 bufferSize, uint8 *sequence, uint16 sequenceSize) {
		for(uint16 i = bufferCheckPoint; i < bufferSize; i++) {
			uint16 j = i - bufferCheckPoint;

			if (j >= sequenceSize) {
				return true;
			}

			if(buffer[i] != sequence[j]) {
				return false;
			}
		}


		return false;
	}






	int sdsdf() {

		uint8 sequence[2] = { 2 };
		uint16 sequenceSize = 1;

		uint8 sequenceReplace[2] = { 9, 1 };
		uint16 sequenceReplaceSize = 2;

		uint8 inBuffer[] = { 0, 1, 2, 3, 4, 5 };
		uint16 inSize = sizeof(inBuffer);


		uint8 outBuffer[255] = { 0 };
		uint16 outSize = sizeof(outBuffer);


		uint16 outBufferShift = 0;




		for(uint16 i = 0; i < inSize; i++) {
			bool check = CheckSequence(&inBuffer[0], i, inSize, &sequence[0], sequenceSize);

			if (check) {

			} else {
				outBuffer[i + outBufferShift] = inBuffer[i];
			}

		}


		for (uint16 i = 0; i < outBufferShift + inSize; i++) {
			//std::cout << (int)outBuffer[i] << std::endl;
		}


		//std::cout << std::hex << crc << std::endl;




		std::system("pause");
	}


};
