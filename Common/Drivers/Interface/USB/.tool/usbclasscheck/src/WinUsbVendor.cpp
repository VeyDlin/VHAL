#include "WinUsbVendor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "DemoProducts.h"
#include "DeviceIdentity.h"
#include "DeviceTree.h"
#include "TransferLogic.h"
#include "WinText.h"

namespace {

// A block of up to 64 KB takes about 60 ms at full speed. The timeout is generous, so a stalled device fails the check
// and does not hang it.
constexpr DWORD RoundTripTimeoutMs = 10000;

// The wait for an aborted or a cancelled request. The request ends at once once the driver has cancelled it; the wait only covers a driver
// that is slow to report it.
constexpr DWORD AbortWaitMs = 1000;

// The bulk pipe type in the WinUSB pipe information (UsbdPipeTypeBulk).
constexpr int BulkPipeType = 2;


// Owns a manual-reset Win32 event handle.
class ScopedEvent {
private:
	HANDLE handle = nullptr;


public:
	ScopedEvent() {
		handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	}


	ScopedEvent(const ScopedEvent&) = delete;
	ScopedEvent& operator=(const ScopedEvent&) = delete;


	~ScopedEvent() {
		if (handle != nullptr) {
			CloseHandle(handle);
		}
	}


	HANDLE Get() const {
		return handle;
	}
};


// One overlapped request of the round trip: the event it signals, its OVERLAPPED structure and whether it is still in progress. The
// buffer and the OVERLAPPED structure belong to the request until it has ended.
struct Request {
	ScopedEvent event;
	OVERLAPPED overlapped {};
	bool pending = false;
};


// Starts an IN read or an OUT write on a bulk pipe. A request that completed at once is signalled through its event, so every request
// is ended the same way: the round trip waits for the event and then asks for the result.
bool StartRequest(
	WINUSB_INTERFACE_HANDLE interfaceHandle,
	std::uint8_t pipeId,
	bool isRead,
	std::uint8_t* buffer,
	ULONG length,
	Request& request,
	const std::string& name,
	std::string& error)
{
	ResetEvent(request.event.Get());
	request.overlapped = OVERLAPPED {};
	request.overlapped.hEvent = request.event.Get();

	BOOL completed = FALSE;
	if (isRead) {
		completed = WinUsb_ReadPipe(interfaceHandle, pipeId, buffer, length, nullptr, &request.overlapped);
	} else {
		completed = WinUsb_WritePipe(interfaceHandle, pipeId, buffer, length, nullptr, &request.overlapped);
	}

	if (!completed && GetLastError() != ERROR_IO_PENDING) {
		error = name + " did not start: " + FormatWindowsError(GetLastError());
		return false;
	}

	if (completed) {
		SetEvent(request.event.Get());
	}

	request.pending = true;
	return true;
}


// Starts an IN read for the bytes of the block that are still missing, at the end of what has arrived.
bool StartInRead(
	WINUSB_INTERFACE_HANDLE interfaceHandle,
	std::vector<std::uint8_t>& received,
	std::size_t blockBytes,
	std::size_t receivedBytes,
	Request& read,
	std::string& error)
{
	ULONG length = static_cast<ULONG>(RemainingBytes(blockBytes, receivedBytes));
	return StartRequest(
		interfaceHandle,
		DemoProducts::CompositeVendorBulkInAddress,
		true,
		received.data() + receivedBytes,
		length,
		read,
		"the IN read",
		error);
}


// Takes the result of a request whose event is signalled. The request is no longer pending after this call.
bool FinishRequest(
	WINUSB_INTERFACE_HANDLE interfaceHandle,
	Request& request,
	const std::string& name,
	DWORD& bytes,
	std::string& error)
{
	request.pending = false;
	bytes = 0;
	if (!WinUsb_GetOverlappedResult(interfaceHandle, &request.overlapped, &bytes, FALSE)) {
		error = name + " failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	return true;
}


// Waits up to timeoutMs for a pending request to end. True when the request has ended or was not pending; it is then no longer pending.
bool WaitForEnd(Request& request, DWORD timeoutMs) {
	if (!request.pending) {
		return true;
	}

	if (WaitForSingleObject(request.event.Get(), timeoutMs) != WAIT_OBJECT_0) {
		return false;
	}

	request.pending = false;
	return true;
}


// Aborts both bulk pipes and waits a bounded time for the requests that are still pending. A request that is still pending after that is
// cancelled on its own with CancelIoEx, and waited for once more. True when both requests have ended. A request that has not ended keeps
// its pending flag, and the caller must then keep its memory alive.
bool AbortAndWait(HANDLE device, WINUSB_INTERFACE_HANDLE interfaceHandle, Request& read, Request& write) {
	WinUsb_AbortPipe(interfaceHandle, DemoProducts::CompositeVendorBulkInAddress);
	WinUsb_AbortPipe(interfaceHandle, DemoProducts::CompositeVendorBulkOutAddress);
	bool readEnded = WaitForEnd(read, AbortWaitMs);
	bool writeEnded = WaitForEnd(write, AbortWaitMs);
	if (readEnded && writeEnded) {
		return true;
	}

	if (read.pending) {
		CancelIoEx(device, &read.overlapped);
	}

	if (write.pending) {
		CancelIoEx(device, &write.overlapped);
	}

	readEnded = WaitForEnd(read, AbortWaitMs);
	writeEnded = WaitForEnd(write, AbortWaitMs);
	return readEnded && writeEnded;
}

}


bool FindVendorPath(const std::string& serial, std::string& path, bool& found, std::string& error) {
	GUID interfaceGuid {};
	if (!ParseGuidText(DemoProducts::CompositeInterfaceGuid, interfaceGuid)) {
		error = "the composite's interface GUID is not a valid GUID";
		return false;
	}

	std::vector<std::string> paths;
	if (!EnumerateInterfacePaths(interfaceGuid, paths, error)) {
		return false;
	}

	found = false;
	for (const std::string& candidate : paths) {
		std::string instance = InstanceFromInterfacePath(candidate);
		InstanceParts parts;
		if (!ParseInstance(instance, parts)) {
			continue;
		}

		if (parts.vendorId != DemoProducts::VendorId || parts.productId != DemoProducts::CompositeProductId) {
			continue;
		}

		if (parts.interfaceNumber != DemoProducts::CompositeVendorInterface) {
			continue;
		}

		std::string boardSerial;
		bool boardFound = false;
		if (!SerialOfInstance(instance, boardSerial, boardFound, error)) {
			return false;
		}

		if (boardFound && SerialMatches(serial, boardSerial)) {
			path = candidate;
			found = true;
			return true;
		}
	}

	return true;
}


VendorChannel::~VendorChannel() {
	Close();
}


bool VendorChannel::Open(const std::string& path, std::string& error) {
	Close();
	std::wstring widePath = WidenUtf8(path);
	device = CreateFileW(
		widePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
		nullptr);
	if (device == INVALID_HANDLE_VALUE) {
		error = "cannot open the vendor interface: " + FormatWindowsError(GetLastError());
		return false;
	}

	if (!WinUsb_Initialize(device, &interfaceHandle)) {
		error = "WinUsb_Initialize failed: " + FormatWindowsError(GetLastError());
		Close();
		return false;
	}

	return true;
}


void VendorChannel::Close() {
	if (interfaceHandle != nullptr) {
		WinUsb_Free(interfaceHandle);
		interfaceHandle = nullptr;
	}

	if (device != INVALID_HANDLE_VALUE) {
		CloseHandle(device);
		device = INVALID_HANDLE_VALUE;
	}
}


bool VendorChannel::CheckEndpoints(std::string& error) {
	if (interfaceHandle == nullptr) {
		error = "the vendor interface is not open";
		return false;
	}

	USB_INTERFACE_DESCRIPTOR iface {};
	if (!WinUsb_QueryInterfaceSettings(interfaceHandle, 0, &iface)) {
		error = "WinUsb_QueryInterfaceSettings failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	bool inFound = false;
	bool outFound = false;
	for (UCHAR index = 0; index < iface.bNumEndpoints; index++) {
		WINUSB_PIPE_INFORMATION pipe {};
		if (!WinUsb_QueryPipe(interfaceHandle, 0, index, &pipe)) {
			error = "WinUsb_QueryPipe failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		if (static_cast<int>(pipe.PipeType) != BulkPipeType) {
			continue;
		}

		if (pipe.PipeId == DemoProducts::CompositeVendorBulkInAddress) {
			inFound = true;
		}

		if (pipe.PipeId == DemoProducts::CompositeVendorBulkOutAddress) {
			outFound = true;
		}
	}

	if (!inFound || !outFound) {
		error = "the vendor interface has no bulk endpoints 0x86 IN and 0x07 OUT";
		return false;
	}

	return true;
}


// The device ends a transfer that is a whole number of packets with a zero-length packet, because it does not know the host's read size.
// An echo can therefore come back as several IN completions (a full packet, a zero-length packet, the rest). The IN side reads until the
// block has all its bytes, each read asking for the rest; the OUT side is one request.
bool VendorChannel::RoundTrip(const std::vector<std::uint8_t>& sent, std::vector<std::uint8_t>& received, std::string& error) {
	if (interfaceHandle == nullptr) {
		error = "the vendor interface is not open";
		return false;
	}

	received.clear();
	if (sent.empty()) {
		return true;
	}

	const std::size_t blockBytes = sent.size();
	const ULONG writeLength = static_cast<ULONG>(blockBytes);
	// The requests and the OUT buffer live on the heap, so a request that cannot be ended can keep its memory without moving it.
	std::unique_ptr<std::vector<std::uint8_t>> outOwner = std::make_unique<std::vector<std::uint8_t>>(sent.begin(), sent.end());
	std::vector<std::uint8_t>& outBuffer = *outOwner;
	std::unique_ptr<Request> readOwner = std::make_unique<Request>();
	std::unique_ptr<Request> writeOwner = std::make_unique<Request>();
	Request& read = *readOwner;
	Request& write = *writeOwner;
	received.assign(blockBytes, 0);

	if (read.event.Get() == nullptr || write.event.Get() == nullptr) {
		error = "CreateEvent failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	// Ends the round after an error. A request that is still pending after the abort keeps its memory: the device may still write into the
	// IN buffer and the OVERLAPPED structures, so they are given to the heap and never freed. Only a device that has stopped answering
	// leaks this memory, and it is a few kilobytes, which is less than a write into freed memory.
	auto endRound = [&]() -> void {
		if (AbortAndWait(device, interfaceHandle, read, write)) {
			return;
		}

		if (read.pending) {
			// Swapping moves the storage of the received vector to the heap without moving its bytes, so the IN read keeps its buffer.
			std::vector<std::uint8_t>* keptReceived = new std::vector<std::uint8_t>();
			keptReceived->swap(received);
			readOwner.release();
		}

		if (write.pending) {
			writeOwner.release();
			outOwner.release();
		}
	};

	// The IN read starts first, so the device's answer never waits for the host to read it. The deadline covers the whole round trip,
	// including the reads that follow a zero-length or a short completion.
	ULONGLONG deadline = GetTickCount64() + RoundTripTimeoutMs;
	std::size_t receivedBytes = 0;
	if (!StartInRead(interfaceHandle, received, blockBytes, receivedBytes, read, error)) {
		endRound();
		return false;
	}

	if (!StartRequest(
		interfaceHandle,
		DemoProducts::CompositeVendorBulkOutAddress,
		false,
		outBuffer.data(),
		writeLength,
		write,
		"the OUT write",
		error))
	{
		endRound();
		return false;
	}

	while (read.pending || write.pending) {
		HANDLE events[2] {};
		DWORD count = 0;
		if (read.pending) {
			events[count] = read.event.Get();
			count++;
		}

		if (write.pending) {
			events[count] = write.event.Get();
			count++;
		}

		DWORD waitTime = static_cast<DWORD>(MillisecondsUntil(deadline, GetTickCount64()));
		DWORD waitResult = WaitForMultipleObjects(count, events, FALSE, waitTime);
		if (waitResult == WAIT_TIMEOUT) {
			error = "no completion within " + std::to_string(RoundTripTimeoutMs) + " ms: the device stopped answering ("
				+ std::to_string(receivedBytes) + " of " + std::to_string(blockBytes) + " bytes came back)";
			endRound();
			return false;
		}

		if (waitResult == WAIT_FAILED || waitResult >= WAIT_OBJECT_0 + count) {
			error = "WaitForMultipleObjects failed: " + FormatWindowsError(GetLastError());
			endRound();
			return false;
		}

		HANDLE signaled = events[waitResult - WAIT_OBJECT_0];
		if (signaled == read.event.Get()) {
			DWORD bytes = 0;
			if (!FinishRequest(interfaceHandle, read, "the IN read", bytes, error)) {
				endRound();
				return false;
			}

			if (!AddReadBytes(blockBytes, receivedBytes, bytes)) {
				error = "the IN read returned " + std::to_string(bytes) + " bytes, more than the "
					+ std::to_string(RemainingBytes(blockBytes, receivedBytes)) + " still missing";
				endRound();
				return false;
			}

			// A zero-length or a short completion is not the end of the block, so the rest is asked for again.
			if (receivedBytes < blockBytes && !StartInRead(interfaceHandle, received, blockBytes, receivedBytes, read, error)) {
				endRound();
				return false;
			}
		} else {
			DWORD bytes = 0;
			if (!FinishRequest(interfaceHandle, write, "the OUT write", bytes, error)) {
				endRound();
				return false;
			}

			if (bytes != writeLength) {
				error = "the OUT write sent " + std::to_string(bytes) + " of " + std::to_string(writeLength) + " bytes";
				endRound();
				return false;
			}
		}
	}

	if (receivedBytes != blockBytes) {
		error = "the IN read returned " + std::to_string(receivedBytes) + " of " + std::to_string(blockBytes) + " bytes";
		return false;
	}

	return true;
}
