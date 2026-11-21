#pragma once

#include <cstddef>
#include <cstdint>

// The pure part of the vendor loopback and of the timed waits. The Win32 code calls these, and the tests run them without a board.

// The number of bytes the next IN read asks for: the rest of the block. A read that asks for only the rest cannot bring more bytes
// than the block has left, so a device that sends more is an error.
std::size_t RemainingBytes(std::size_t blockBytes, std::size_t receivedBytes);

// Adds the bytes of one completed IN read to receivedBytes. False when the read brought more bytes than the block has left, and then
// receivedBytes is not changed. A zero-length or a short read is not an error: the caller asks for the rest again.
bool AddReadBytes(std::size_t blockBytes, std::size_t& receivedBytes, std::size_t readBytes);

// The milliseconds from now until the deadline, and zero once it has passed. Both are GetTickCount64 values.
std::uint64_t MillisecondsUntil(std::uint64_t deadline, std::uint64_t now);
