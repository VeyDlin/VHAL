// expect: floating-point values are not supported
#include <Utilities/Data/ByteConverter.h>


// A float has no byte order here: the value type check stops the program's compilation with a message.
int main() {
	uint8 bytes[4] = {};
	ByteConverter::Put<float, ByteOrder::big>(bytes, 0, 1.0f);
	return 0;
}
