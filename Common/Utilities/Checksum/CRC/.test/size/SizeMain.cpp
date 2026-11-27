#include <Utilities/DataTypes.h>
#include <cstddef>

// The program's entry: it calls the size function on a buffer the compiler cannot see through, so no CRC is folded at compile time.
// The buffer is the same in every program, so it costs the same and cancels in the difference.


uint64 SizeRun(const uint8* data, std::size_t length);


static uint8 sizeBuffer[32];


int main() {
	for (std::size_t index = 0; index < sizeof(sizeBuffer); index++) {
		sizeBuffer[index] = static_cast<uint8>(index * 7);
	}

	return static_cast<int>(SizeRun(sizeBuffer, sizeof(sizeBuffer)) & 0xFF);
}
