#include <Utilities/DataTypes.h>
#include <cstddef>

// The entry point of every size program. Each program makes the same call on the same buffer, so the buffer costs the same in every
// program and cancels in the difference.


uint64 SizeRun(const uint8* data, std::size_t length);


static uint8 sizeBuffer[32];


int main() {
	for (std::size_t index = 0; index < sizeof(sizeBuffer); index++) {
		sizeBuffer[index] = static_cast<uint8>(index * 7);
	}

	return static_cast<int>(SizeRun(sizeBuffer, sizeof(sizeBuffer)) & 0xFF);
}
