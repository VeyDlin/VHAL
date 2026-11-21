#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// The IEEE CRC-32 of zlib, Windows and the Python zlib module: reflected polynomial 0xEDB88320, initial value and final
// XOR 0xFFFFFFFF. The DFU demo prints this value for the bytes of a download.
std::uint32_t Crc32(const std::uint8_t* data, std::size_t size);

std::uint32_t Crc32(const std::vector<std::uint8_t>& data);
