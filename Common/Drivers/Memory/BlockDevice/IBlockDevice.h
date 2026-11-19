#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// A medium of equal blocks: a RAM disk, a flash disk, an SD card. A mass storage function reads and writes it, and an
// application may use it for any other block consumer.
//
// Threads: every call is thread context and may block, because a flash or card access takes time. An interrupt must never
// call these functions. The mass storage function calls them only from its Process().
//
// Results: Read, Write and Sync return ok or the error. IsPresent() returns ok when a medium is inserted, and notExist when
// it is not. IsReadOnly() returns ok when the medium accepts writes, and readOnly when it does not.
//
// Geometry: GetBlockSize() and GetBlockCount() are constant for the life of the object, and they may be called from any
// context. A medium of another size is a different device object.
//
// Ranges: Read and Write take whole blocks. data.size() must be a multiple of the block size (invalidParameter), and the
// range must lie inside the device (outOfRange). A call on a medium that is not present returns notExist.
class IBlockDevice {
public:
	virtual ~IBlockDevice() = default;


	// Bytes per block, the same for every block.
	virtual uint32 GetBlockSize() const = 0;


	// Number of blocks on the device. Block numbers run from 0 to GetBlockCount() - 1.
	virtual uint32 GetBlockCount() const = 0;


	// Reads data.size() / GetBlockSize() blocks starting at firstBlock into data.
	virtual ResultStatus Read(uint32 firstBlock, std::span<uint8> data) = 0;


	// Writes data.size() / GetBlockSize() blocks starting at firstBlock. Returns readOnly when the medium is write protected.
	virtual ResultStatus Write(uint32 firstBlock, std::span<const uint8> data) = 0;


	// Makes the written blocks durable. A medium with no write cache returns ok at once.
	virtual ResultStatus Sync() = 0;


	// ok when the medium accepts writes, readOnly when it is write protected.
	virtual ResultStatus IsReadOnly() = 0;


	// ok when a medium is inserted, notExist when it is not.
	virtual ResultStatus IsPresent() = 0;


protected:
	// The argument check of Read and Write, shared by the implementations: a whole number of blocks, and a range that lies
	// inside the device. Returns ok, invalidParameter or outOfRange.
	static ResultStatus CheckRange(uint32 blockSize, uint32 blockCount, uint32 firstBlock, std::size_t byteCount) {
		if (blockSize == 0 || byteCount % blockSize != 0) {
			return ResultStatus::invalidParameter;
		}

		std::size_t blocks = byteCount / blockSize;

		if (static_cast<std::size_t>(firstBlock) + blocks > blockCount) {
			return ResultStatus::outOfRange;
		}

		return ResultStatus::ok;
	}
};
