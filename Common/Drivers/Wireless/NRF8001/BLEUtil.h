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

};

