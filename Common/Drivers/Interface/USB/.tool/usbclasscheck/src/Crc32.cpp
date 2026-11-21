#include "Crc32.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

std::array<std::uint32_t, 256> MakeTable() {
	std::array<std::uint32_t, 256> table {};
	for (std::uint32_t index = 0; index < 256; index++) {
		std::uint32_t value = index;
		for (int bit = 0; bit < 8; bit++) {
			if ((value & 1) != 0) {
				value = 0xEDB88320U ^ (value >> 1);
			} else {
				value = value >> 1;
			}
		}

		table[index] = value;
	}

	return table;
}

}


std::uint32_t Crc32(const std::uint8_t* data, std::size_t size) {
	static const std::array<std::uint32_t, 256> table = MakeTable();
	std::uint32_t crc = 0xFFFFFFFFU;
	for (std::size_t index = 0; index < size; index++) {
		crc = table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
	}

	return crc ^ 0xFFFFFFFFU;
}


std::uint32_t Crc32(const std::vector<std::uint8_t>& data) {
	return Crc32(data.data(), data.size());
}
