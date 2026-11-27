#include <Utilities/DataTypes.h>
#include <cstddef>

// The baseline: the same call as the size programs, with no CRC in it.


uint64 SizeRun(const uint8* data, std::size_t length) {
	uint64 sum = 0;
	for (std::size_t index = 0; index < length; index++) {
		sum += data[index];
	}

	return sum;
}
