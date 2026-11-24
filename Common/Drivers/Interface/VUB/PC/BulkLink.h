#pragma once
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/PC/TransferRelease.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <vector>
#include <winusb.h>
#elif defined(__linux__)
#include <libusb-1.0/libusb.h>
#include <sys/time.h>
#else
#error "VUB::PC::BulkLink is written for Windows and Linux only"
#endif

namespace VUB::PC {


// The vendor bulk byte stream of a device (VendorBulkFunction). On Windows the device is found by its device interface GUID through
// SetupAPI, with an optional serial number of the USB device that holds the interface, and opened through WinUSB. On Linux the device is
// found by its vendor and product IDs, with an optional serial number, and the interface is claimed through libusb.
//
// Reads and writes are asynchronous with the link's own buffers: one read is always pending (overlapped on Windows, a transfer on
// Linux), and a write is pending until its transfer completes. Poll never waits; on Linux it handles the transfers with a zero
// timeout. A read or write that fails reports the link down and closes the device without waiting: the transfers in flight are
// cancelled, and Poll releases the device once they have completed. Open again to come back. The link is reliable.
//
// Close waits up to 200 ms for the transfers in flight; one still in flight after that is released by Poll. The destructor waits until
// every transfer has completed. Open returns busy while an earlier release is still waiting for its transfers.
class BulkLink {
public:
	// The device to open. Windows uses interfaceGuid, serial, inEndpoint and outEndpoint. Linux uses vendorId, productId,
	// interfaceNumber, serial, inEndpoint and outEndpoint. serial is optional: an empty one matches any device.
	struct Settings {
		std::string interfaceGuid;
		std::string serial;
		std::uint16_t vendorId = 0;
		std::uint16_t productId = 0;
		std::uint8_t interfaceNumber = 0;
		std::uint8_t inEndpoint = 0x86;
		std::uint8_t outEndpoint = 0x07;
	};


	static constexpr LinkTraits traits = LinkTraits().Reliable().FullDuplex().ReportsState().MaxPacket(256);


private:
	// The handle layer of a release of this link (see TransferRelease).
	class ReleaseLayer {
	public:
		explicit ReleaseLayer(BulkLink& linkObject) : link(linkObject) {
		}


		void CancelAll() {
			link.CancelTransfers();
		}


		bool InFlight() const {
			return link.TransfersInFlight();
		}


		void Progress(uint32 milliseconds) {
			link.ProgressTransfers(milliseconds);
		}


		void Free() {
			link.FreeHandles();
		}


	private:
		BulkLink& link;
	};


	static constexpr std::size_t transferBytes = 64;
	StateSink sink;
	bool portOpen = false;
	std::uint8_t inPipe = 0x86;
	std::uint8_t outPipe = 0x07;
	std::array<std::uint8_t, transferBytes> readBuffer{};
	std::array<std::uint8_t, transferBytes> writeBuffer{};
	std::size_t readOffset = 0;
	std::size_t readAvailable = 0;
	TransferRelease<ReleaseLayer> release;
#if defined(_WIN32)
	HANDLE device = INVALID_HANDLE_VALUE;
	WINUSB_INTERFACE_HANDLE usb = nullptr;
	HANDLE readEvent = nullptr;
	HANDLE writeEvent = nullptr;
	OVERLAPPED readOverlapped{};
	OVERLAPPED writeOverlapped{};
	bool readPending = false;
	bool writePending = false;
#else
	libusb_context* context = nullptr;
	libusb_device_handle* handle = nullptr;
	std::uint8_t interfaceNumber = 0;
	libusb_transfer* readTransfer = nullptr;
	libusb_transfer* writeTransfer = nullptr;
	bool readInFlight = false;
	bool writeInFlight = false;
	bool readArrived = false;
	bool writeArrived = false;
	int readStatus = 0;
	int writeStatus = 0;
	int readLength = 0;
#endif


public:
	BulkLink() = default;


	BulkLink(const BulkLink&) = delete;
	BulkLink& operator=(const BulkLink&) = delete;


	~BulkLink() {
		Shut(ReleaseWait::Unbounded);
	}


	// Whether a device serial matches the wanted one. An empty wanted serial matches any device.
	static bool SerialMatches(std::string_view wanted, std::string_view found) {
		return wanted.empty() || wanted == found;
	}


#if defined(_WIN32)
	// Reads a GUID in the form {8-4-4-4-12} (the braces and the hyphens are optional). False when the text is not a GUID.
	static bool ParseGuid(const std::string& text, GUID& guid) {
		std::array<std::uint8_t, 16> bytes{};
		std::size_t count = 0;
		int high = -1;
		for (char symbol : text) {
			if (symbol == '{' || symbol == '}' || symbol == '-') {
				continue;
			}

			int value = HexValue(symbol);
			if (value < 0 || count >= 32) {
				return false;
			}

			if (high < 0) {
				high = value;
			} else {
				bytes[count / 2] = static_cast<std::uint8_t>(high * 16 + value);
				high = -1;
			}

			count++;
		}

		if (count != 32 || high >= 0) {
			return false;
		}

		guid.Data1 = (static_cast<DWORD>(bytes[0]) << 24) | (static_cast<DWORD>(bytes[1]) << 16) | (static_cast<DWORD>(bytes[2]) << 8) | bytes[3];
		guid.Data2 = static_cast<WORD>((bytes[4] << 8) | bytes[5]);
		guid.Data3 = static_cast<WORD>((bytes[6] << 8) | bytes[7]);
		for (std::size_t index = 0; index < 8; index++) {
			guid.Data4[index] = bytes[8 + index];
		}

		return true;
	}
#endif


	// Opens the device. Returns ok when the device is open and up; an error otherwise, and the link stays closed. Returns busy while an
	// earlier release still waits for its transfers.
	ResultStatus Open(const Settings& settings) {
		Close();
		if (release.Closing()) {
			return ResultStatus::busy;
		}

		inPipe = settings.inEndpoint;
		outPipe = settings.outEndpoint;
#if defined(_WIN32)
		GUID interfaceId{};
		if (!ParseGuid(settings.interfaceGuid, interfaceId)) {
			return ResultStatus::invalidParameter;
		}

		std::wstring path;
		ResultStatus found = FindPath(interfaceId, settings.serial, path);
		if (found != ResultStatus::ok) {
			return found;
		}

		device = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
		if (device == INVALID_HANDLE_VALUE) {
			device = INVALID_HANDLE_VALUE;
			return ResultStatus::deviceOrResourceBusy;
		}

		if (!WinUsb_Initialize(device, &usb)) {
			usb = nullptr;
			Shut(ReleaseWait::None);
			return ResultStatus::ioError;
		}

		readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#else
		if (libusb_init(&context) != 0) {
			context = nullptr;
			return ResultStatus::ioError;
		}

		libusb_device** devices = nullptr;
		ssize_t count = libusb_get_device_list(context, &devices);
		ResultStatus result = ResultStatus::notFound;
		for (ssize_t index = 0; index < count && handle == nullptr; index++) {
			libusb_device_descriptor descriptor{};
			if (libusb_get_device_descriptor(devices[index], &descriptor) != 0) {
				continue;
			}

			if (descriptor.idVendor != settings.vendorId || descriptor.idProduct != settings.productId) {
				continue;
			}

			libusb_device_handle* opened = nullptr;
			if (libusb_open(devices[index], &opened) != 0) {
				result = ResultStatus::deviceOrResourceBusy;
				continue;
			}

			if (!SerialOfHandle(opened, descriptor.iSerialNumber, settings.serial)) {
				libusb_close(opened);
				continue;
			}

			if (libusb_claim_interface(opened, settings.interfaceNumber) != 0) {
				libusb_close(opened);
				result = ResultStatus::deviceOrResourceBusy;
				continue;
			}

			handle = opened;
			interfaceNumber = settings.interfaceNumber;
			result = ResultStatus::ok;
		}

		libusb_free_device_list(devices, 1);
		if (handle == nullptr) {
			Shut(ReleaseWait::None);
			return result;
		}

		readTransfer = libusb_alloc_transfer(0);
		writeTransfer = libusb_alloc_transfer(0);
		if (readTransfer == nullptr || writeTransfer == nullptr) {
			Shut(ReleaseWait::None);
			return ResultStatus::outOfMemory;
		}
#endif
		portOpen = true;
		sink.Report(true);
		StartRead();
		if (!portOpen) {
			return ResultStatus::ioError;
		}

		return ResultStatus::ok;
	}


	// Closes the device. Waits up to 200 ms for the transfers in flight; one still in flight after that is released by Poll. Reports down
	// when it was up.
	void Close() {
		bool wasOpen = portOpen;
		Shut(ReleaseWait::Bounded);
		if (wasOpen) {
			sink.Report(false);
		}
	}


	bool IsOpen() const {
		return portOpen;
	}


	// The stack gives its sink when it is built, which may be after Open. A stack starts with its link down, so a link that is up
	// reports it into the sink at once.
	void SetStateSink(StateSink value) {
		sink = value;
		if (IsOpen()) {
			sink.Report(true);
		}
	}


	// Feeds the bytes that have arrived, up to the receiver's room, and starts the next read. Never waits.
	void Poll(Receiver receiver) {
		ReleaseLayer layer(*this);
		release.Finish(layer, ReleaseWait::None);
		if (!portOpen) {
			return;
		}

		Pump();
		TakeRead();
		if (!portOpen) {
			return;
		}

		std::size_t room = receiver.Room();
		if (readAvailable > 0 && room > 0) {
			std::size_t take = readAvailable < room ? readAvailable : room;
			receiver.Feed(std::span<const uint8>(readBuffer.data() + readOffset, take));
			readOffset += take;
			readAvailable -= take;
		}

		if (readAvailable == 0 && !ReadBusy()) {
			StartRead();
		}
	}


	// Starts a write of up to 64 bytes and returns how many it took. Returns zero while the previous write is pending.
	Result<std::size_t> Write(std::span<const uint8> bytes) {
		if (!portOpen || bytes.empty()) {
			return std::size_t{0};
		}

		Pump();
		if (!TakeWrite()) {
			return std::size_t{0};
		}

		std::size_t count = bytes.size() < transferBytes ? bytes.size() : transferBytes;
		for (std::size_t index = 0; index < count; index++) {
			writeBuffer[index] = bytes[index];
		}

		if (!StartWrite(count)) {
			return std::size_t{0};
		}

		return count;
	}


private:
	// Handles the transfers that finished (Linux). Windows completes them through GetOverlappedResult, so there is nothing to do.
	void Pump() {
#if defined(_WIN32)
#else
		if (context == nullptr) {
			return;
		}

		timeval wait{};
		libusb_handle_events_timeout_completed(context, &wait, nullptr);
#endif
	}


	// Takes the read that finished, into the read buffer. Closes the device when the read failed.
	void TakeRead() {
#if defined(_WIN32)
		if (!readPending) {
			return;
		}

		DWORD got = 0;
		if (!WinUsb_GetOverlappedResult(usb, &readOverlapped, &got, FALSE)) {
			if (GetLastError() != ERROR_IO_INCOMPLETE) {
				Abort();
			}

			return;
		}

		readPending = false;
		readOffset = 0;
		readAvailable = got;
#else
		if (!readArrived) {
			return;
		}

		readArrived = false;
		if (readStatus != static_cast<int>(LIBUSB_TRANSFER_COMPLETED)) {
			Abort();
			return;
		}

		readOffset = 0;
		readAvailable = static_cast<std::size_t>(readLength);
#endif
	}


	// Whether a read is on its way to the buffer.
	bool ReadBusy() const {
#if defined(_WIN32)
		return readPending;
#else
		return readInFlight;
#endif
	}


	void StartRead() {
#if defined(_WIN32)
		ResetEvent(readEvent);
		readOverlapped = OVERLAPPED{};
		readOverlapped.hEvent = readEvent;
		if (!WinUsb_ReadPipe(usb, inPipe, readBuffer.data(), static_cast<ULONG>(readBuffer.size()), nullptr, &readOverlapped) && GetLastError() != ERROR_IO_PENDING) {
			Abort();
			return;
		}

		readPending = true;
#else
		libusb_fill_bulk_transfer(readTransfer, handle, inPipe, readBuffer.data(), static_cast<int>(readBuffer.size()), &OnReadDone, this, 0);
		if (libusb_submit_transfer(readTransfer) != 0) {
			Abort();
			return;
		}

		readInFlight = true;
		readArrived = false;
#endif
	}


	// Whether the previous write is done, so that another can start. Closes the device when that write failed.
	bool TakeWrite() {
#if defined(_WIN32)
		if (!writePending) {
			return true;
		}

		DWORD sent = 0;
		if (!WinUsb_GetOverlappedResult(usb, &writeOverlapped, &sent, FALSE)) {
			if (GetLastError() != ERROR_IO_INCOMPLETE) {
				Abort();
			}

			return false;
		}

		writePending = false;
		return true;
#else
		if (writeArrived) {
			writeArrived = false;
			if (writeStatus != static_cast<int>(LIBUSB_TRANSFER_COMPLETED)) {
				Abort();
				return false;
			}
		}

		return portOpen && !writeInFlight;
#endif
	}


	bool StartWrite(std::size_t count) {
#if defined(_WIN32)
		ResetEvent(writeEvent);
		writeOverlapped = OVERLAPPED{};
		writeOverlapped.hEvent = writeEvent;
		if (!WinUsb_WritePipe(usb, outPipe, writeBuffer.data(), static_cast<ULONG>(count), nullptr, &writeOverlapped) && GetLastError() != ERROR_IO_PENDING) {
			Abort();
			return false;
		}

		writePending = true;
		return true;
#else
		libusb_fill_bulk_transfer(writeTransfer, handle, outPipe, writeBuffer.data(), static_cast<int>(count), &OnWriteDone, this, 0);
		if (libusb_submit_transfer(writeTransfer) != 0) {
			Abort();
			return false;
		}

		writeInFlight = true;
		writeArrived = false;
		return true;
#endif
	}


	// Closes the device from an I/O path. It cancels the transfers in flight and does not wait: Poll releases the device once they have
	// completed. Reports down when it was up.
	void Abort() {
		bool wasOpen = portOpen;
		Shut(ReleaseWait::None);
		if (wasOpen) {
			sink.Report(false);
		}
	}


	// Stops the link: no more transfers, and the handles are freed once no transfer is in flight, waiting as the wait allows. Does not report.
	void Shut(ReleaseWait wait) {
		portOpen = false;
		readOffset = 0;
		readAvailable = 0;
		ReleaseLayer layer(*this);
		release.Begin(layer);
		release.Finish(layer, wait);
	}


	// Cancels the transfers in flight. Their completion is seen later through TransfersInFlight.
	void CancelTransfers() {
#if defined(_WIN32)
		if (usb != nullptr) {
			WinUsb_AbortPipe(usb, inPipe);
			WinUsb_AbortPipe(usb, outPipe);
		}
#else
		if (readInFlight) {
			libusb_cancel_transfer(readTransfer);
		}

		if (writeInFlight) {
			libusb_cancel_transfer(writeTransfer);
		}
#endif
	}


	// Whether a transfer is still in flight when this is called. Never waits.
	bool TransfersInFlight() const {
#if defined(_WIN32)
		bool readWaiting = readPending && !HasOverlappedIoCompleted(&readOverlapped);
		bool writeWaiting = writePending && !HasOverlappedIoCompleted(&writeOverlapped);
		return readWaiting || writeWaiting;
#else
		return readInFlight || writeInFlight;
#endif
	}


	// Handles the completions that are ready, waiting up to the given time for the transfers in flight (zero does not wait).
	void ProgressTransfers(uint32 milliseconds) {
#if defined(_WIN32)
		HANDLE waitFor[2] = {nullptr, nullptr};
		DWORD count = 0;
		if (readPending && !HasOverlappedIoCompleted(&readOverlapped)) {
			waitFor[count] = readEvent;
			count++;
		}

		if (writePending && !HasOverlappedIoCompleted(&writeOverlapped)) {
			waitFor[count] = writeEvent;
			count++;
		}

		if (count > 0) {
			WaitForMultipleObjects(count, waitFor, TRUE, milliseconds);
		}
#else
		if (context == nullptr) {
			return;
		}

		timeval wait{};
		wait.tv_sec = static_cast<time_t>(milliseconds / 1000);
		wait.tv_usec = static_cast<suseconds_t>((milliseconds % 1000) * 1000);
		libusb_handle_events_timeout_completed(context, &wait, nullptr);
#endif
	}


	// Frees the device and its handles. Called only when no transfer is in flight.
	void FreeHandles() {
#if defined(_WIN32)
		if (usb != nullptr) {
			WinUsb_Free(usb);
			usb = nullptr;
		}

		if (device != INVALID_HANDLE_VALUE) {
			CloseHandle(device);
			device = INVALID_HANDLE_VALUE;
		}

		if (readEvent != nullptr) {
			CloseHandle(readEvent);
			readEvent = nullptr;
		}

		if (writeEvent != nullptr) {
			CloseHandle(writeEvent);
			writeEvent = nullptr;
		}

		readPending = false;
		writePending = false;
#else
		if (readTransfer != nullptr) {
			libusb_free_transfer(readTransfer);
			readTransfer = nullptr;
		}

		if (writeTransfer != nullptr) {
			libusb_free_transfer(writeTransfer);
			writeTransfer = nullptr;
		}

		if (handle != nullptr) {
			libusb_release_interface(handle, interfaceNumber);
			libusb_close(handle);
			handle = nullptr;
		}

		if (context != nullptr) {
			libusb_exit(context);
			context = nullptr;
		}

		readInFlight = false;
		writeInFlight = false;
		readArrived = false;
		writeArrived = false;
#endif
	}


#if defined(_WIN32)
	static int HexValue(char symbol) {
		if (symbol >= '0' && symbol <= '9') {
			return symbol - '0';
		}

		if (symbol >= 'a' && symbol <= 'f') {
			return symbol - 'a' + 10;
		}

		if (symbol >= 'A' && symbol <= 'F') {
			return symbol - 'A' + 10;
		}

		return -1;
	}


	// Finds the path of the first interface with the GUID whose USB device has the wanted serial.
	static ResultStatus FindPath(const GUID& interfaceId, const std::string& serial, std::wstring& path) {
		HDEVINFO set = SetupDiGetClassDevsW(&interfaceId, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (set == INVALID_HANDLE_VALUE) {
			return ResultStatus::ioError;
		}

		ResultStatus result = ResultStatus::notFound;
		for (DWORD index = 0;; index++) {
			SP_DEVICE_INTERFACE_DATA interfaceData{};
			interfaceData.cbSize = sizeof(interfaceData);
			if (!SetupDiEnumDeviceInterfaces(set, nullptr, &interfaceId, index, &interfaceData)) {
				break;
			}

			SP_DEVINFO_DATA deviceData{};
			deviceData.cbSize = sizeof(deviceData);
			DWORD required = 0;
			SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, nullptr, 0, &required, nullptr);
			std::vector<BYTE> storage(required);
			SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
			detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
			if (!SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, detail, required, nullptr, &deviceData)) {
				continue;
			}

			if (!SerialMatches(serial, SerialOfNode(deviceData.DevInst))) {
				continue;
			}

			path = detail->DevicePath;
			result = ResultStatus::ok;
			break;
		}

		SetupDiDestroyDeviceInfoList(set);
		return result;
	}


	// The serial number of the USB device above an interface node: the last part of the parent's device ID, after its last backslash.
	static std::string SerialOfNode(DEVINST node) {
		DEVINST parent = 0;
		if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) {
			return std::string();
		}

		wchar_t parentId[MAX_DEVICE_ID_LEN] = {};
		if (CM_Get_Device_IDW(parent, parentId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
			return std::string();
		}

		std::wstring text(parentId);
		std::size_t slash = text.rfind(L'\\');
		if (slash == std::wstring::npos) {
			return std::string();
		}

		std::string serialText;
		for (std::size_t index = slash + 1; index < text.size(); index++) {
			serialText.push_back(static_cast<char>(text[index]));
		}

		return serialText;
	}
#else
	// Reads the serial number of an open device and compares it with the wanted one. An empty wanted serial matches any device.
	static bool SerialOfHandle(libusb_device_handle* opened, std::uint8_t index, const std::string& wanted) {
		if (wanted.empty()) {
			return true;
		}

		std::array<unsigned char, 256> text{};
		int length = libusb_get_string_descriptor_ascii(opened, index, text.data(), static_cast<int>(text.size()));
		if (length <= 0) {
			return false;
		}

		std::string found(reinterpret_cast<const char*>(text.data()), static_cast<std::size_t>(length));
		return SerialMatches(wanted, found);
	}


	// The transfer callbacks: the transfer's buffer is the link's own, and the result is kept until Poll or Write takes it.
	static void LIBUSB_CALL OnReadDone(libusb_transfer* transfer) {
		BulkLink* self = static_cast<BulkLink*>(transfer->user_data);
		self->readInFlight = false;
		self->readArrived = true;
		self->readStatus = static_cast<int>(transfer->status);
		self->readLength = transfer->actual_length;
	}


	static void LIBUSB_CALL OnWriteDone(libusb_transfer* transfer) {
		BulkLink* self = static_cast<BulkLink*>(transfer->user_data);
		self->writeInFlight = false;
		self->writeArrived = true;
		self->writeStatus = static_cast<int>(transfer->status);
	}
#endif
};

}
