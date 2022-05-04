#pragma once
#define MAX_UUID_LENGTH 16

class BLEUuid {
public:
	 BLEUuid(const char *str) : _str(str) {
		char temp[] = { 0, 0, 0 };

		this->_length = 0;
		for (int i = strlen(str) - 1; i >= 0 && this->_length < MAX_UUID_LENGTH; i -= 2) {
			if (str[i] == '-') {
				i++;
				continue;
			}

			temp[0] = str[i - 1];
			temp[1] = str[i];

			this->_data[this->_length] = strtoul(temp, NULL, 16);

			this->_length++;
		}
	}

	const char*  str() const {
		return this->_str;
	}

	const unsigned char*  data() const {
		return this->_data;
	}

	unsigned char  length() const {
		return this->_length;
	}

private:
	const char *_str;
	unsigned char _data[MAX_UUID_LENGTH];
	unsigned char _length;
};
