#pragma once
#include <string>

class BLEUtil {
public:
	// TODO: std::string

	static void addressToString(const unsigned char *in, char *out) {
		std::string address = "";
		std::string hex;

		for (int i = 5; i >= 0; i--) {
			if (in[i] < 0x10) {
				address += "0";
			}

			hex = std::string(in[i], 16);
			address += hex;

			if (i > 0) {
				address += ":";
			}
		}

		// address.toCharArray(out, 18);
		strncpy(out, address.c_str(), 18);
	}

	static void printBuffer(const unsigned char *buffer, unsigned char length) {
		for (int i = 0; i < length; i++) {
			if ((buffer[i] & 0xf0) == 00) {
				System::log.Write("0");
			}

			System::log.Write(buffer[i], Print::Hex);
			System::log.Write(" ");
		}
		System::log.Line();
	}

};

