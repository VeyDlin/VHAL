#include "WinUsbBackend.h"

#include <setupapi.h>
#include <objbase.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#include "DeviceSelection.h"
#include "Options.h"
#include "Text.h"

namespace {

// How long an aborted request may take to end. A stalled request already ends by itself after BulkTimeoutMs, so two
// timeouts is a generous margin. A request that is still pending after it stops the process (see StopWithPendingRequest).
constexpr DWORD AbortWaitMs = 2 * BulkTimeoutMs;

// Every overlapped request of a RunBulk call is waited on in one WaitForMultipleObjects call, two lanes wide.
static_assert(
	2 * MaxInflight <= static_cast<std::uint32_t>(MAXIMUM_WAIT_OBJECTS),
	"two lanes of MaxInflight transfers must fit in one WaitForMultipleObjects call");

// Owns a Win32 event handle.
class ScopedEvent {
private:
	HANDLE handle = nullptr;


public:
	explicit ScopedEvent(HANDLE value) {
		handle = value;
	}


	ScopedEvent(ScopedEvent&& other) noexcept {
		handle = other.handle;
		other.handle = nullptr;
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


	bool IsValid() const {
		return handle != nullptr;
	}
};


// Owns a SetupAPI device information set.
class ScopedDeviceInfo {
private:
	HDEVINFO handle = INVALID_HANDLE_VALUE;


public:
	explicit ScopedDeviceInfo(HDEVINFO value) {
		handle = value;
	}


	ScopedDeviceInfo(const ScopedDeviceInfo&) = delete;
	ScopedDeviceInfo& operator=(const ScopedDeviceInfo&) = delete;


	~ScopedDeviceInfo() {
		if (handle != INVALID_HANDLE_VALUE) {
			SetupDiDestroyDeviceInfoList(handle);
		}
	}


	HDEVINFO Get() const {
		return handle;
	}


	bool IsValid() const {
		return handle != INVALID_HANDLE_VALUE;
	}
};


// One bulk direction of a run: its endpoint, the lane that holds the data, and one overlapped request per slot.
struct WinUsbLane {
	BulkLane* lane = nullptr;
	std::uint8_t endpoint = 0;
	bool readDirection = false;
	std::vector<OVERLAPPED> overlapped;
	std::vector<ScopedEvent> events;
	std::vector<bool> pending;
};

// A request that is in flight, found through the event the wait returned.
struct PendingTransfer {
	WinUsbLane* state = nullptr;
	std::size_t slot = 0;
};


std::string FormatWindowsError(DWORD code) {
	char* message = nullptr;
	DWORD length = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPSTR>(&message),
		0,
		nullptr);

	std::string text;
	if (length > 0 && message != nullptr) {
		text.assign(message, length);
		while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == '.')) {
			text.pop_back();
		}
	}

	if (message != nullptr) {
		LocalFree(message);
	}

	AppendFormat(text, " (error %lu)", static_cast<unsigned long>(code));
	return text;
}


std::wstring WidenAscii(const char* text) {
	std::wstring wide;
	for (const char* position = text; *position != '\0'; position++) {
		wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*position)));
	}

	return wide;
}


std::wstring LowerCase(std::wstring text) {
	for (wchar_t& character : text) {
		character = static_cast<wchar_t>(std::towlower(character));
	}

	return text;
}


// Device paths are ASCII. Anything else is shown as '?', the way libusb shows a non-ASCII string.
std::string NarrowAscii(const std::wstring& text) {
	std::string narrow;
	for (wchar_t character : text) {
		narrow.push_back(character < 0x80 ? static_cast<char>(character) : '?');
	}

	return narrow;
}


// The location column of `list`. An interface path is \\?\usb#vid_..&pid_..#<instance>#{guid}: the instance segment is the
// device instance ID, which differs between two boards on the bus. A path of another shape is shown whole.
std::string InstanceText(const std::wstring& path) {
	std::size_t first = path.find(L'#');
	std::size_t second = first == std::wstring::npos ? std::wstring::npos : path.find(L'#', first + 1);
	std::size_t third = second == std::wstring::npos ? std::wstring::npos : path.find(L'#', second + 1);
	if (third == std::wstring::npos) {
		return NarrowAscii(path);
	}

	return "instance " + NarrowAscii(path.substr(second + 1, third - second - 1));
}


// Reads the device path of one interface, which contains the vendor and product IDs.
bool ReadInterfacePath(HDEVINFO deviceInfo, SP_DEVICE_INTERFACE_DATA& interfaceData, std::wstring& path, std::string& error) {
	DWORD requiredSize = 0;
	SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
	if (requiredSize == 0) {
		error = "SetupDiGetDeviceInterfaceDetail failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	std::vector<std::uint8_t> storage(requiredSize);
	SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
	detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
	if (!SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, detail, requiredSize, nullptr, nullptr)) {
		error = "SetupDiGetDeviceInterfaceDetail failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	path = detail->DevicePath;
	return true;
}


// Device paths of every interface with the benchmark GUID and IDs, in SetupAPI order.
bool FindDevicePaths(std::vector<std::wstring>& paths, std::string& error) {
	GUID interfaceGuid {};
	std::wstring guidText = WidenAscii(BenchProtocol::InterfaceGuid);
	if (CLSIDFromString(guidText.c_str(), &interfaceGuid) != S_OK) {
		error = "BenchProtocol::InterfaceGuid is not a valid GUID";
		return false;
	}

	// Device paths are lower case and contain the IDs as "vid_xxxx&pid_xxxx".
	wchar_t idFilter[64] {};
	std::swprintf(
		idFilter,
		sizeof(idFilter) / sizeof(idFilter[0]),
		L"vid_%04x&pid_%04x",
		static_cast<unsigned int>(BenchProtocol::VendorId),
		static_cast<unsigned int>(BenchProtocol::ProductId));

	ScopedDeviceInfo deviceInfo(SetupDiGetClassDevsW(&interfaceGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
	if (!deviceInfo.IsValid()) {
		error = "SetupDiGetClassDevs failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	paths.clear();
	for (DWORD index = 0;; index++) {
		SP_DEVICE_INTERFACE_DATA interfaceData {};
		interfaceData.cbSize = sizeof(interfaceData);
		if (!SetupDiEnumDeviceInterfaces(deviceInfo.Get(), nullptr, &interfaceGuid, index, &interfaceData)) {
			if (GetLastError() == ERROR_NO_MORE_ITEMS) {
				break;
			}

			error = "SetupDiEnumDeviceInterfaces failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		std::wstring path;
		if (!ReadInterfacePath(deviceInfo.Get(), interfaceData, path, error)) {
			return false;
		}

		if (LowerCase(path).find(idFilter) != std::wstring::npos) {
			paths.push_back(path);
		}
	}

	return true;
}


// Frees the interface and closes the device, and leaves both handles empty.
void ReleasePath(HANDLE& device, WINUSB_INTERFACE_HANDLE& interfaceHandle) {
	if (interfaceHandle != nullptr) {
		WinUsb_Free(interfaceHandle);
		interfaceHandle = nullptr;
	}

	if (device != INVALID_HANDLE_VALUE) {
		CloseHandle(device);
		device = INVALID_HANDLE_VALUE;
	}
}


// Opens one device path and WinUSB on its interface. On failure both handles are left empty.
bool OpenPath(const std::wstring& path, HANDLE& device, WINUSB_INTERFACE_HANDLE& interfaceHandle, std::string& error) {
	device = CreateFileW(
		path.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
		nullptr);
	if (device == INVALID_HANDLE_VALUE) {
		error = "cannot open the benchmark device: " + FormatWindowsError(GetLastError());
		return false;
	}

	// The first interface of the device is the benchmark interface (BenchProtocol::InterfaceNumber 0).
	interfaceHandle = nullptr;
	if (!WinUsb_Initialize(device, &interfaceHandle)) {
		error = "WinUsb_Initialize failed: " + FormatWindowsError(GetLastError());
		ReleasePath(device, interfaceHandle);
		return false;
	}

	// Set before anything reads a descriptor (Enumerate and Open both do), so a device that never answers those reads
	// fails within ControlTimeoutMs instead of hanging. The default control pipe has ID 0.
	ULONG controlTimeout = ControlTimeoutMs;
	if (!WinUsb_SetPipePolicy(interfaceHandle, 0, PIPE_TRANSFER_TIMEOUT, sizeof(controlTimeout), &controlTimeout)) {
		error = "WinUsb_SetPipePolicy(control PIPE_TRANSFER_TIMEOUT) failed: " + FormatWindowsError(GetLastError());
		ReleasePath(device, interfaceHandle);
		return false;
	}

	return true;
}


// The English (US) language ID. The benchmark firmware has one language, and libusb's ASCII string helper uses the same.
constexpr USHORT EnglishLanguageId = 0x0409;


// Reads the serial number string through the interface, the same string libusb reads with its ASCII helper. On failure
// `problem` says why and `serial` stays empty.
bool ReadSerial(WINUSB_INTERFACE_HANDLE interfaceHandle, std::string& serial, std::string& problem) {
	USB_DEVICE_DESCRIPTOR descriptor {};
	ULONG transferred = 0;
	if (!WinUsb_GetDescriptor(
		interfaceHandle,
		USB_DEVICE_DESCRIPTOR_TYPE,
		0,
		0,
		reinterpret_cast<PUCHAR>(&descriptor),
		sizeof(descriptor),
		&transferred)) {
		problem = "WinUsb_GetDescriptor(device) failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	if (descriptor.iSerialNumber == 0) {
		problem = "the device reports no serial number";
		return false;
	}

	std::uint8_t buffer[256] {};
	transferred = 0;
	if (!WinUsb_GetDescriptor(
		interfaceHandle,
		USB_STRING_DESCRIPTOR_TYPE,
		descriptor.iSerialNumber,
		EnglishLanguageId,
		buffer,
		sizeof(buffer),
		&transferred)) {
		problem = "the serial string could not be read: " + FormatWindowsError(GetLastError());
		return false;
	}

	// A string descriptor is its length in bytes, its type, then UTF-16LE code units.
	std::size_t length = std::min(static_cast<std::size_t>(buffer[0]), static_cast<std::size_t>(transferred));
	std::string text;
	for (std::size_t index = 2; index + 1 < length; index += 2) {
		std::uint32_t unit = static_cast<std::uint32_t>(buffer[index]) | (static_cast<std::uint32_t>(buffer[index + 1]) << 8);
		text.push_back(unit < 0x80 ? static_cast<char>(unit) : '?');
	}

	if (text.empty()) {
		problem = "the serial string is empty";
		return false;
	}

	serial = text;
	return true;
}


// Pipe policies of the bulk endpoints. RAW_IO hands requests straight to the host controller, which is what keeps the
// throughput up; every request length must then be a multiple of the packet size, which usbbench checks first. The
// control pipe's timeout is set in OpenPath, before any descriptor read.
bool ConfigurePipes(WINUSB_INTERFACE_HANDLE interfaceHandle, std::string& error) {
	const std::uint8_t endpoints[2] = {
		BenchProtocol::BulkInAddress,
		BenchProtocol::BulkOutAddress
	};
	UCHAR rawIo = 1;
	UCHAR autoClearStall = 1;
	ULONG timeout = BulkTimeoutMs;

	for (std::uint8_t endpoint : endpoints) {
		if (!WinUsb_SetPipePolicy(interfaceHandle, endpoint, RAW_IO, sizeof(rawIo), &rawIo)) {
			error = "WinUsb_SetPipePolicy(RAW_IO) failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		if (!WinUsb_SetPipePolicy(interfaceHandle, endpoint, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout)) {
			error = "WinUsb_SetPipePolicy(PIPE_TRANSFER_TIMEOUT) failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		if (!WinUsb_SetPipePolicy(interfaceHandle, endpoint, AUTO_CLEAR_STALL, sizeof(autoClearStall), &autoClearStall)) {
			error = "WinUsb_SetPipePolicy(AUTO_CLEAR_STALL) failed: " + FormatWindowsError(GetLastError());
			return false;
		}
	}

	return true;
}


bool PrepareLane(WinUsbLane& state, BulkLane* lane, std::uint8_t endpoint, bool readDirection, std::string& error) {
	state.lane = lane;
	state.endpoint = endpoint;
	state.readDirection = readDirection;

	std::size_t slotCount = lane->GetSlotCount();
	state.overlapped.assign(slotCount, OVERLAPPED {});
	state.pending.assign(slotCount, false);
	state.events.reserve(slotCount);
	for (std::size_t slot = 0; slot < slotCount; slot++) {
		state.events.emplace_back(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		if (!state.events.back().IsValid()) {
			error = "CreateEvent failed: " + FormatWindowsError(GetLastError());
			return false;
		}
	}

	return true;
}


bool FinishTransfer(WINUSB_INTERFACE_HANDLE interfaceHandle, WinUsbLane& state, std::size_t slot, std::string& error);

// Hands the transfer to WinUSB. A request that completes at once is finished here, so the caller never waits for it.
bool StartTransfer(WINUSB_INTERFACE_HANDLE interfaceHandle, WinUsbLane& state, const BulkTransfer& transfer, std::string& error) {
	OVERLAPPED& overlapped = state.overlapped[transfer.slot];
	overlapped = OVERLAPPED {};
	overlapped.hEvent = state.events[transfer.slot].Get();
	if (!ResetEvent(overlapped.hEvent)) {
		error = "ResetEvent failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	ULONG length = static_cast<ULONG>(transfer.length);
	BOOL started = FALSE;
	if (state.readDirection) {
		started = WinUsb_ReadPipe(interfaceHandle, state.endpoint, transfer.data, length, nullptr, &overlapped);
	} else {
		started = WinUsb_WritePipe(interfaceHandle, state.endpoint, transfer.data, length, nullptr, &overlapped);
	}

	if (started) {
		return FinishTransfer(interfaceHandle, state, transfer.slot, error);
	}

	if (GetLastError() != ERROR_IO_PENDING) {
		error = "transfer on endpoint " + std::to_string(state.endpoint) + " did not start: " + FormatWindowsError(GetLastError());
		return false;
	}

	state.pending[transfer.slot] = true;
	return true;
}


bool FinishTransfer(WINUSB_INTERFACE_HANDLE interfaceHandle, WinUsbLane& state, std::size_t slot, std::string& error) {
	DWORD transferred = 0;
	state.pending[slot] = false;
	if (!WinUsb_GetOverlappedResult(interfaceHandle, &state.overlapped[slot], &transferred, FALSE)) {
		error = "transfer on endpoint " + std::to_string(state.endpoint) + " failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	std::size_t requested = state.lane->GetSlotLength(slot);
	if (static_cast<std::size_t>(transferred) != requested) {
		AppendFormat(
			error,
			"short transfer on endpoint 0x%02X: %lu of %lu bytes",
			static_cast<unsigned int>(state.endpoint),
			static_cast<unsigned long>(transferred),
			static_cast<unsigned long>(requested));
		return false;
	}

	state.lane->CompleteSubmit(slot);
	return true;
}


bool SubmitLanes(WINUSB_INTERFACE_HANDLE interfaceHandle, const std::vector<WinUsbLane*>& lanes, std::string& error) {
	for (WinUsbLane* state : lanes) {
		while (state->lane->CanSubmit()) {
			BulkTransfer transfer;
			state->lane->BeginSubmit(transfer);
			if (!StartTransfer(interfaceHandle, *state, transfer, error)) {
				return false;
			}
		}
	}

	return true;
}


bool AllFinished(const std::vector<WinUsbLane*>& lanes) {
	for (const WinUsbLane* state : lanes) {
		if (!state->lane->IsFinished()) {
			return false;
		}
	}

	return true;
}


bool AnyPending(const std::vector<WinUsbLane*>& lanes) {
	for (const WinUsbLane* state : lanes) {
		for (bool pending : state->pending) {
			if (pending) {
				return true;
			}
		}
	}

	return false;
}


// The events of every request in flight, and the request each event belongs to, in the same order.
void CollectPending(const std::vector<WinUsbLane*>& lanes, std::vector<HANDLE>& handles, std::vector<PendingTransfer>& owners) {
	handles.clear();
	owners.clear();
	for (WinUsbLane* state : lanes) {
		for (std::size_t slot = 0; slot < state->pending.size(); slot++) {
			if (state->pending[slot]) {
				PendingTransfer owner;
				owner.state = state;
				owner.slot = slot;
				handles.push_back(state->events[slot].Get());
				owners.push_back(owner);
			}
		}
	}
}


// Stops the process. A request that was aborted and did not end still owns the buffer and OVERLAPPED of the RunBulk or
// DrainBulkIn call; returning would free them while WinUSB may still write into them, so there is no safe way back.
[[noreturn]] void StopWithPendingRequest(const char* what) {
	std::fprintf(
		stderr,
		"usbbench: %s did not end within %lu ms after the abort; stopping so no buffer is freed while WinUSB uses it\n",
		what,
		static_cast<unsigned long>(AbortWaitMs));
	std::abort();
}


// Waits until one request of any lane completes, then finishes it.
bool WaitAndFinish(WINUSB_INTERFACE_HANDLE interfaceHandle, const std::vector<WinUsbLane*>& lanes, std::string& error) {
	std::vector<HANDLE> handles;
	std::vector<PendingTransfer> owners;
	CollectPending(lanes, handles, owners);

	if (handles.empty()) {
		error = "internal error: no transfer is in flight";
		return false;
	}

	DWORD count = static_cast<DWORD>(handles.size());
	DWORD waitResult = WaitForMultipleObjects(count, handles.data(), FALSE, BulkTimeoutMs);
	if (waitResult == WAIT_TIMEOUT) {
		error = "no completion within " + std::to_string(BulkTimeoutMs) + " ms; the device stopped responding";
		return false;
	}

	if (waitResult == WAIT_FAILED || waitResult >= WAIT_OBJECT_0 + count) {
		error = "WaitForMultipleObjects failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	const PendingTransfer& owner = owners[waitResult - WAIT_OBJECT_0];
	return FinishTransfer(interfaceHandle, *owner.state, owner.slot, error);
}


// Stops every request still in flight after a failure, and returns only once none is left. The buffers and OVERLAPPED
// structures they point to belong to the RunBulk call, so no request may outlive it.
void AbortPending(WINUSB_INTERFACE_HANDLE interfaceHandle, const std::vector<WinUsbLane*>& lanes) {
	for (WinUsbLane* state : lanes) {
		for (bool pending : state->pending) {
			if (pending) {
				WinUsb_AbortPipe(interfaceHandle, state->endpoint);
				break;
			}
		}
	}

	std::vector<HANDLE> handles;
	std::vector<PendingTransfer> owners;
	CollectPending(lanes, handles, owners);
	while (!handles.empty()) {
		DWORD count = static_cast<DWORD>(handles.size());
		DWORD waitResult = WaitForMultipleObjects(count, handles.data(), FALSE, AbortWaitMs);
		if (waitResult >= WAIT_OBJECT_0 + count) {
			StopWithPendingRequest("a WinUSB request");
		}

		const PendingTransfer& owner = owners[waitResult - WAIT_OBJECT_0];
		DWORD transferred = 0;
		WinUsb_GetOverlappedResult(interfaceHandle, &owner.state->overlapped[owner.slot], &transferred, FALSE);
		owner.state->pending[owner.slot] = false;
		CollectPending(lanes, handles, owners);
	}
}


const char* PipeTypeName(int pipeType) {
	switch (pipeType) {
		case 0:
			return "control";
		case 1:
			return "isochronous";
		case 2:
			return "bulk";
		case 3:
			return "interrupt";
	}

	return "unknown";
}

}


WinUsbBackend::~WinUsbBackend() {
	Close();
}


bool WinUsbBackend::Enumerate(std::vector<UsbDeviceInfo>& devices, std::string& error) {
	std::vector<std::wstring> paths;
	if (!FindDevicePaths(paths, error)) {
		return false;
	}

	devices.clear();
	for (const std::wstring& path : paths) {
		UsbDeviceInfo info;
		info.location = InstanceText(path);

		HANDLE device = INVALID_HANDLE_VALUE;
		WINUSB_INTERFACE_HANDLE interfaceHandle = nullptr;
		std::string openError;
		if (OpenPath(path, device, interfaceHandle, openError)) {
			ReadSerial(interfaceHandle, info.serial, info.problem);
		} else {
			info.problem = openError;
		}

		ReleasePath(device, interfaceHandle);
		devices.push_back(info);
	}

	return true;
}


bool WinUsbBackend::Open(const std::string& serial, std::string& error) {
	Close();

	std::vector<std::wstring> paths;
	if (!FindDevicePaths(paths, error)) {
		return false;
	}

	OpenFailures failures;
	failures.serial = serial;
	for (const std::wstring& path : paths) {
		std::string openError;
		if (!OpenPath(path, deviceHandle, interfaceHandle, openError)) {
			// Without an open handle the serial cannot be read, so this device may be the one that was asked for.
			failures.unreadable++;
			failures.readError = openError;
			continue;
		}

		// Several boards share the IDs, so the serial number decides which one stays open.
		if (!serial.empty()) {
			std::string found;
			std::string problem;
			if (!ReadSerial(interfaceHandle, found, problem)) {
				Close();
				failures.unreadable++;
				failures.readError = problem;
				continue;
			}

			if (!SameSerial(found, serial)) {
				Close();
				continue;
			}
		}

		if (!ConfigurePipes(interfaceHandle, error)) {
			Close();
			return false;
		}

		return true;
	}

	error = OpenFailureText(failures);
	return false;
}


void WinUsbBackend::Close() {
	ReleasePath(deviceHandle, interfaceHandle);
}


bool WinUsbBackend::Describe(std::string& text, std::string& error) {
	if (interfaceHandle == nullptr) {
		error = "the device is not open";
		return false;
	}

	USB_DEVICE_DESCRIPTOR descriptor {};
	ULONG transferred = 0;
	if (!WinUsb_GetDescriptor(
		interfaceHandle,
		USB_DEVICE_DESCRIPTOR_TYPE,
		0,
		0,
		reinterpret_cast<PUCHAR>(&descriptor),
		sizeof(descriptor),
		&transferred)) {
		error = "WinUsb_GetDescriptor(device) failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	AppendFormat(
		text,
		"Device        %04X:%04X, USB %X.%02X, release %X.%02X\n",
		static_cast<unsigned int>(descriptor.idVendor),
		static_cast<unsigned int>(descriptor.idProduct),
		static_cast<unsigned int>(descriptor.bcdUSB >> 8),
		static_cast<unsigned int>(descriptor.bcdUSB & 0xFF),
		static_cast<unsigned int>(descriptor.bcdDevice >> 8),
		static_cast<unsigned int>(descriptor.bcdDevice & 0xFF));
	AppendFormat(
		text,
		"Class         %02X:%02X:%02X, control packet %u bytes\n",
		static_cast<unsigned int>(descriptor.bDeviceClass),
		static_cast<unsigned int>(descriptor.bDeviceSubClass),
		static_cast<unsigned int>(descriptor.bDeviceProtocol),
		static_cast<unsigned int>(descriptor.bMaxPacketSize0));

	USB_INTERFACE_DESCRIPTOR iface {};
	if (!WinUsb_QueryInterfaceSettings(interfaceHandle, 0, &iface)) {
		error = "WinUsb_QueryInterfaceSettings failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	AppendFormat(
		text,
		"Interface %u: class %02X:%02X:%02X, %u endpoint(s)\n",
		static_cast<unsigned int>(iface.bInterfaceNumber),
		static_cast<unsigned int>(iface.bInterfaceClass),
		static_cast<unsigned int>(iface.bInterfaceSubClass),
		static_cast<unsigned int>(iface.bInterfaceProtocol),
		static_cast<unsigned int>(iface.bNumEndpoints));

	for (UCHAR pipeIndex = 0; pipeIndex < iface.bNumEndpoints; pipeIndex++) {
		WINUSB_PIPE_INFORMATION pipe {};
		if (!WinUsb_QueryPipe(interfaceHandle, 0, pipeIndex, &pipe)) {
			error = "WinUsb_QueryPipe failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		AppendFormat(
			text,
			"  Endpoint 0x%02X %s, max packet %u bytes\n",
			static_cast<unsigned int>(pipe.PipeId),
			PipeTypeName(static_cast<int>(pipe.PipeType)),
			static_cast<unsigned int>(pipe.MaximumPacketSize));
	}

	return true;
}


bool WinUsbBackend::ControlIn(
	BenchProtocol::Request request,
	std::uint16_t value,
	std::span<std::uint8_t> data,
	std::size_t& received,
	std::string& error)
{
	if (interfaceHandle == nullptr) {
		error = "the device is not open";
		return false;
	}

	WINUSB_SETUP_PACKET setup {};
	setup.RequestType = VendorRequestIn;
	setup.Request = static_cast<UCHAR>(request);
	setup.Value = value;
	setup.Index = BenchProtocol::InterfaceNumber;
	setup.Length = static_cast<USHORT>(data.size());

	ULONG transferred = 0;
	if (!WinUsb_ControlTransfer(interfaceHandle, setup, data.data(), static_cast<ULONG>(data.size()), &transferred, nullptr)) {
		error = "control request failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	received = static_cast<std::size_t>(transferred);
	return true;
}


bool WinUsbBackend::ControlOut(BenchProtocol::Request request, std::uint16_t value, std::string& error) {
	if (interfaceHandle == nullptr) {
		error = "the device is not open";
		return false;
	}

	WINUSB_SETUP_PACKET setup {};
	setup.RequestType = VendorRequestOut;
	setup.Request = static_cast<UCHAR>(request);
	setup.Value = value;
	setup.Index = BenchProtocol::InterfaceNumber;
	setup.Length = 0;

	ULONG transferred = 0;
	if (!WinUsb_ControlTransfer(interfaceHandle, setup, nullptr, 0, &transferred, nullptr)) {
		error = "control request failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	return true;
}


bool WinUsbBackend::DrainBulkIn(std::string& error) {
	if (interfaceHandle == nullptr) {
		error = "the device is not open";
		return false;
	}

	std::vector<std::uint8_t> buffer(DrainChunkBytes);
	ScopedEvent event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
	if (!event.IsValid()) {
		error = "CreateEvent failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	std::size_t drained = 0;
	while (true) {
		OVERLAPPED overlapped {};
		overlapped.hEvent = event.Get();
		if (!ResetEvent(event.Get())) {
			error = "ResetEvent failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		BOOL started = WinUsb_ReadPipe(
			interfaceHandle,
			BenchProtocol::BulkInAddress,
			buffer.data(),
			static_cast<ULONG>(buffer.size()),
			nullptr,
			&overlapped);
		if (!started && GetLastError() != ERROR_IO_PENDING) {
			error = "drain read did not start: " + FormatWindowsError(GetLastError());
			return false;
		}

		if (!started) {
			DWORD waitResult = WaitForSingleObject(event.Get(), DrainQuietMs);
			if (waitResult == WAIT_TIMEOUT) {
				// Quiet: abort the read that waits for data. It must end before the event and the OVERLAPPED go away.
				WinUsb_AbortPipe(interfaceHandle, BenchProtocol::BulkInAddress);
				if (WaitForSingleObject(event.Get(), AbortWaitMs) != WAIT_OBJECT_0) {
					StopWithPendingRequest("the drain read");
				}

				DWORD ignored = 0;
				WinUsb_GetOverlappedResult(interfaceHandle, &overlapped, &ignored, FALSE);
				return true;
			}

			if (waitResult != WAIT_OBJECT_0) {
				// Read the error of this wait before anything else: the abort below replaces the last error.
				DWORD waitError = GetLastError();
				WinUsb_AbortPipe(interfaceHandle, BenchProtocol::BulkInAddress);
				if (WaitForSingleObject(event.Get(), AbortWaitMs) != WAIT_OBJECT_0) {
					StopWithPendingRequest("the drain read");
				}

				error = "drain wait failed: " + FormatWindowsError(waitError);
				return false;
			}
		}

		DWORD transferred = 0;
		if (!WinUsb_GetOverlappedResult(interfaceHandle, &overlapped, &transferred, FALSE)) {
			error = "drain read failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		drained += static_cast<std::size_t>(transferred);
		if (drained > MaxDrainBytes) {
			error = "the device keeps sending stream data after it was set to idle";
			return false;
		}
	}
}


bool WinUsbBackend::RunBulk(BulkLane* inLane, BulkLane* outLane, std::string& error) {
	if (interfaceHandle == nullptr) {
		error = "the device is not open";
		return false;
	}

	WinUsbLane inState;
	WinUsbLane outState;
	std::vector<WinUsbLane*> lanes;
	if (inLane != nullptr) {
		if (!PrepareLane(inState, inLane, BenchProtocol::BulkInAddress, true, error)) {
			return false;
		}

		lanes.push_back(&inState);
	}

	if (outLane != nullptr) {
		if (!PrepareLane(outState, outLane, BenchProtocol::BulkOutAddress, false, error)) {
			return false;
		}

		lanes.push_back(&outState);
	}

	bool ok = true;
	while (true) {
		// Retired slots are refilled in the same pass, so a finished transfer never leaves its slot idle for a wait.
		for (WinUsbLane* state : lanes) {
			state->lane->VerifyCompleted();
		}

		if (!SubmitLanes(interfaceHandle, lanes, error)) {
			ok = false;
			break;
		}

		if (AllFinished(lanes)) {
			break;
		}

		// A request that completed at once is not pending. It is retired on the next pass, so there is nothing to wait for.
		if (!AnyPending(lanes)) {
			continue;
		}

		if (!WaitAndFinish(interfaceHandle, lanes, error)) {
			ok = false;
			break;
		}
	}

	if (!ok) {
		AbortPending(interfaceHandle, lanes);
	}

	return ok;
}
