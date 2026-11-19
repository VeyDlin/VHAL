#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <libusb.h>

#include "UsbBackend.h"

// Portable backend on libusb-1.0. On Windows libusb talks to the device through its WinUSB backend.
class LibUsbBackend : public IUsbBackend {
private:
	libusb_context* context = nullptr;
	libusb_device_handle* handle = nullptr;


public:
	LibUsbBackend() = default;

	LibUsbBackend(const LibUsbBackend&) = delete;

	LibUsbBackend& operator=(const LibUsbBackend&) = delete;

	~LibUsbBackend() override;


	bool Open(std::string& error) override;


	void Close() override;


	bool Describe(std::string& text, std::string& error) override;


	bool ControlIn(
		BenchProtocol::Request request,
		std::uint16_t value,
		std::span<std::uint8_t> data,
		std::size_t& received,
		std::string& error) override;


	bool ControlOut(BenchProtocol::Request request, std::uint16_t value, std::string& error) override;


	bool DrainBulkIn(std::string& error) override;


	bool RunBulk(BulkLane* inLane, BulkLane* outLane, std::string& error) override;


private:
	bool EnsureContext(std::string& error);
};
