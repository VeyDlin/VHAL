#include <Utilities/DataTypes.h>
#include <cstddef>

// The baseline program: the same call with no ring. The cost of a program is the program minus this one.


uint64 SizeRun(const uint8* data, std::size_t length) {
	return static_cast<uint64>(data[0]) + length;
}
