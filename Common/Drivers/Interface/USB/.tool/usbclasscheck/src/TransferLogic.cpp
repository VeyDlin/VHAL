#include "TransferLogic.h"

#include <cstddef>
#include <cstdint>


std::size_t RemainingBytes(std::size_t blockBytes, std::size_t receivedBytes) {
	if (receivedBytes >= blockBytes) {
		return 0;
	}

	return blockBytes - receivedBytes;
}


bool AddReadBytes(std::size_t blockBytes, std::size_t& receivedBytes, std::size_t readBytes) {
	if (readBytes > RemainingBytes(blockBytes, receivedBytes)) {
		return false;
	}

	receivedBytes += readBytes;
	return true;
}


std::uint64_t MillisecondsUntil(std::uint64_t deadline, std::uint64_t now) {
	if (now >= deadline) {
		return 0;
	}

	return deadline - now;
}
