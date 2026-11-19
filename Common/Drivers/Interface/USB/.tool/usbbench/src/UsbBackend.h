#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "BenchProtocol.h"
#include "BulkLane.h"
#include "Text.h"

enum class BackendKind : std::uint8_t {
	LibUsb,
	WinUsb
};

// Every bulk transfer times out after this long: libusb sets it per transfer, WinUSB as the pipe policy. A device that
// stops answering fails the run instead of hanging it, and an aborted transfer also ends within this time.
inline constexpr std::uint32_t BulkTimeoutMs = 5000;
// Vendor requests (BenchProtocol::Request) time out after this long, on both backends.
inline constexpr std::uint32_t ControlTimeoutMs = 2000;
// The drain reads bulk IN until one read gets nothing for this long.
inline constexpr std::uint32_t DrainQuietMs = 50;
inline constexpr std::size_t DrainChunkBytes = 16384;
// More stale data than this during the drain means the device did not stop streaming when told to.
inline constexpr std::size_t MaxDrainBytes = 1024 * 1024;
// bmRequestType of the vendor requests to the benchmark interface.
inline constexpr std::uint8_t VendorRequestIn = 0xC1;
inline constexpr std::uint8_t VendorRequestOut = 0x41;

// Access to the benchmark device. Both backends issue the same vendor requests and move bulk data on the same
// endpoints (BenchProtocol); only the USB API underneath differs.
class IUsbBackend {
public:
	virtual ~IUsbBackend() = default;

	// Finds the device by BenchProtocol::VendorId/ProductId, opens it and claims BenchProtocol::InterfaceNumber.
	virtual bool Open(std::string& error) = 0;

	virtual void Close() = 0;

	// Appends a summary of the device and its descriptors to `text`.
	virtual bool Describe(std::string& text, std::string& error) = 0;

	virtual bool ControlIn(
		BenchProtocol::Request request,
		std::uint16_t value,
		std::span<std::uint8_t> data,
		std::size_t& received,
		std::string& error) = 0;

	virtual bool ControlOut(BenchProtocol::Request request, std::uint16_t value, std::string& error) = 0;

	// Reads bulk IN until it is quiet, discarding the data. Used before a run to clear stale stream data.
	virtual bool DrainBulkIn(std::string& error) = 0;

	// Runs the lanes that are not null with several transfers in flight each. Either lane may be null. Returns only
	// after every transfer it started has finished, on success and on failure, so the lanes and the backend's
	// buffers are never used by the USB stack after the call.
	virtual bool RunBulk(BulkLane* inLane, BulkLane* outLane, std::string& error) = 0;
};

// Creates the backend. The winusb backend exists on Windows only.
std::unique_ptr<IUsbBackend> CreateBackend(BackendKind kind, std::string& error);

const char* BackendName(BackendKind kind);

// The text the user sees when the benchmark device is not attached.
inline std::string NoDeviceMessage() {
	std::string text;
	AppendFormat(
		text,
		"Benchmark device %04X:%04X (interface %u) is not attached. Connect the board and try again.",
		static_cast<unsigned int>(BenchProtocol::VendorId),
		static_cast<unsigned int>(BenchProtocol::ProductId),
		static_cast<unsigned int>(BenchProtocol::InterfaceNumber)
	);
	return text;
}
