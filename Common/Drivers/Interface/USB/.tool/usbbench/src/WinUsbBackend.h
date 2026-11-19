#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <windows.h>
#include <winusb.h>

#include "UsbBackend.h"

// Native WinUSB backend. SetupAPI finds the device by BenchProtocol::InterfaceGuid, WinUSB moves the data with
// overlapped requests, and several requests are in flight per direction.
class WinUsbBackend : public IUsbBackend {
private:
	HANDLE deviceHandle = INVALID_HANDLE_VALUE;
	WINUSB_INTERFACE_HANDLE interfaceHandle = nullptr;


public:
	WinUsbBackend() = default;

	WinUsbBackend(const WinUsbBackend&) = delete;

	WinUsbBackend& operator=(const WinUsbBackend&) = delete;

	~WinUsbBackend() override;


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
};
