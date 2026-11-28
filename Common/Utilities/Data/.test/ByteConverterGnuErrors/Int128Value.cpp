// expect: the value type is 8 bytes or less
#include <Utilities/Data/ByteConverter.h>


// Under -std=gnu++23 unsigned __int128 is an integer, and the 64-bit shifts would drop its high half: the size check stops the compilation.
int main() {
	uint8 bytes[16] = {};
	ByteConverter::Put<unsigned __int128, ByteOrder::little>(bytes, 0, 1);
	return 0;
}
