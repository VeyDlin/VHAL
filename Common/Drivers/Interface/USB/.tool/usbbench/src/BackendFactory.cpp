#include "UsbBackend.h"

#include "LibUsbBackend.h"

#if defined(_WIN32)
#include "WinUsbBackend.h"
#endif


std::unique_ptr<IUsbBackend> CreateBackend(BackendKind kind, [[maybe_unused]] std::string& error) {
	if (kind == BackendKind::LibUsb) {
		return std::make_unique<LibUsbBackend>();
	}

#if defined(_WIN32)
	return std::make_unique<WinUsbBackend>();
#else
	error = "the winusb backend exists on Windows only; use --backend libusb";
	return nullptr;
#endif
}


const char* BackendName(BackendKind kind) {
	if (kind == BackendKind::WinUsb) {
		return "winusb";
	}

	return "libusb";
}
