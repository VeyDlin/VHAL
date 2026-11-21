#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>
#include <winusb.h>

// Finds the vendor raw channel of the board with this serial: the interface MI_04 of the composite, through the composite's
// interface GUID. found is false when Windows created no such interface for the board.
bool FindVendorPath(const std::string& serial, std::string& path, bool& found, std::string& error);

// An open WinUSB interface of the vendor channel. The loopback sends a block and reads the same bytes back.
class VendorChannel {
private:
	HANDLE device = INVALID_HANDLE_VALUE;
	WINUSB_INTERFACE_HANDLE interfaceHandle = nullptr;


public:
	VendorChannel() = default;

	VendorChannel(const VendorChannel&) = delete;
	VendorChannel& operator=(const VendorChannel&) = delete;

	~VendorChannel();

	bool Open(const std::string& path, std::string& error);

	void Close();

	// The bulk IN and bulk OUT endpoints of the channel are present with the addresses DemoProducts names.
	bool CheckEndpoints(std::string& error);

	// Writes sent to the OUT endpoint as one request, and reads the same number of bytes from the IN endpoint at once. The IN read is
	// started first, so the device's loopback never waits for the host to read. The IN side keeps reading until the block has all its
	// bytes, because the device may end the answer with a zero-length packet or split it into several completions.
	bool RoundTrip(const std::vector<std::uint8_t>& sent, std::vector<std::uint8_t>& received, std::string& error);
};
