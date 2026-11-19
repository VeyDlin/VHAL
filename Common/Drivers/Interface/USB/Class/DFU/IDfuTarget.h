#pragma once
#include <cstddef>
#include <span>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <Utilities/Result/Result.h>


// The memory a DFU function writes and reads. The function calls it in two places:
//   Begin() and Read() run in interrupt context, from the control request that starts or continues a transfer. Keep them to
//            register and memory reads: no erase, no waiting.
//   Write() and Manifest() run from Process(), in thread context. They may take long: the host waits the poll timeout
//            that GetPollTimeoutMs() reports, and the function answers dfuDNBUSY or dfuMANIFEST meanwhile.
//
// Offsets count from the start of the transfer (the first DFU_DNLOAD or DFU_UPLOAD after the device left dfuIDLE). A download
// is one stream: each Write() continues where the previous one ended, so a target may keep a partial unit between blocks.
//
// The status codes are the ones the host sees: a target reports the failure it had (errERASE, errPROG, errVERIFY, ...) and the
// function moves to dfuERROR with that code.
class IDfuTarget {
public:
	virtual ~IDfuTarget() = default;


	// Starts a transfer on an alternate setting (0 for a device with one memory region). errTARGET when the alternate is not
	// one of the device's.
	virtual DfuStatus Begin(uint8 alternate) = 0;


	// Writes one download block at offset. Returns OK when the bytes are in memory and verified.
	virtual DfuStatus Write(uint32 offset, std::span<const uint8> data) = 0;


	// Reads up to data.size() bytes at offset into data and returns how many it read. A short count ends an upload, so a
	// read past the end of the region returns fewer bytes, or 0. An error is returned as a failed Result.
	virtual Result<uint32> Read(uint32 offset, std::span<uint8> data) = 0;


	// Completes the transfer in memory, for example by flushing a partial unit and starting the new image. Returns OK when
	// the device can go on (dfuMANIFEST-SYNC, or dfuMANIFEST-WAIT-RESET for a device that cannot answer on the bus).
	virtual DfuStatus Manifest() = 0;


	// The time the host should wait before the next DFU_GETSTATUS while the operation runs, in milliseconds.
	virtual uint32 GetPollTimeoutMs(DfuOperation operation) const = 0;


	// Whether the download holds everything the device needs. A zero-length DFU_DNLOAD asks for the manifestation only when
	// this is true; otherwise the device answers errNOTDONE. A device that cannot check its image returns true.
	virtual bool IsComplete() const {
		return true;
	}
};
