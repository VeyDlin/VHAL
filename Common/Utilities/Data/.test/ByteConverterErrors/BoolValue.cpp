// expect: bool has no byte width
#include <Utilities/Data/ByteConverter.h>


// A bool has no byte order: the value type check stops the program's compilation with a message.
int main() {
	uint8 bytes[2] = {};
	ByteConverter::Put<bool, ByteOrder::little>(bytes, 0, true);
	return 0;
}
