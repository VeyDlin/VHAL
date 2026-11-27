#include "SizeAlgorithms.h"
#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Checksum/CRC/Engine/CrcBitwise.h>
#include <Utilities/Checksum/CRC/Engine/CrcNibble.h>
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>

// One size program: the engine named by SIZE_ENGINE, one or three algorithms by SIZE_ALGORITHMS. The script compiles it with -D for
// each row; the baseline program makes the same call with no CRC, so the cost of an engine is the program minus the baseline.


using Engine = Checksum::SIZE_ENGINE;


uint64 SizeRun(const uint8* data, std::size_t length) {
	const std::span<const uint8> bytes(data, length);
#if SIZE_ALGORITHMS == 1
	return Checksum::Crc<SizeAlgorithms::Modbus, Engine>::Calculate(bytes);
#else
	return Checksum::Crc<SizeAlgorithms::Modbus, Engine>::Calculate(bytes)
		^ Checksum::Crc<SizeAlgorithms::IsoHdlc, Engine>::Calculate(bytes)
		^ Checksum::Crc<SizeAlgorithms::Smbus, Engine>::Calculate(bytes);
#endif
}
