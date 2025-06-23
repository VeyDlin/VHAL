#pragma once
#include <string>

class BLEUtil {
public:
	// TODO: std::string

	static void addressToString(const unsigned char *in, char *out) {
		char buffer[18];
		snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
			in[5], in[4], in[3], in[2], in[1], in[0]);
		strncpy(out, buffer, 18);
	}

};

