#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <span>

// The entry point of the size image: one CRC of the input, with the preset of the image. The linker keeps it through -e.
extern "C" uint32 SizePresetsEntry(const uint8* data, uint32 size) {
	return static_cast<uint32>(Checksum::Crc16Modbus::Calculate(std::span<const uint8>(data, size)));
}
