#pragma once
#include <Drivers/Interface/VUB/Link/HidReport.h>
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

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ntdef.h>
#include <hidusage.h>
extern "C" {
#include <hidpi.h>
#include <hidsdi.h>
}
#include <setupapi.h>
#include <vector>
#elif defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>
#else
#error "VUB::PC::HidLink is written for Windows and Linux only"
#endif

namespace VUB::PC {


// A HID device's report link, with the same report layout as the device's HidLink (Link/HidReport.h): a length byte, the payload and
// zero padding, ReportBytes in all. A packet of up to ReportBytes - 1 bytes goes in one report. The reports carry no report ID, so the
// output report has the ID 0 in front of it, as the OS API takes it.
//
// Windows: the device is found by vendor ID, product ID and usage page through hid.dll and SetupAPI; reads and writes are overlapped
// with the link's own buffers. Linux: the device is found through its hidraw node by the raw info (the vendor and product IDs), and the
// usage page is read from its report descriptor; reads and writes are non-blocking. A read or write that fails reports the link down and
// closes the device without waiting: on Windows the transfers in flight are cancelled, and Poll frees the handles once they completed.
//
// Close waits up to 200 ms for the transfers in flight; one still in flight after that is released by Poll. The destructor waits until
// every transfer has completed. Open returns busy while an earlier release is still waiting for its transfers.
template<std::size_t ReportBytes = 64>
class HidLink {
	static_assert(ReportBytes >= 2 && ReportBytes <= 256, "a HID report of the link is 2 to 256 bytes");


public:
	static constexpr LinkTraits traits = LinkTraits().Framed().FullDuplex().ReportsState().MaxPacket(ReportBytes - 1);


private:
	// The handle layer of a release of this link (see TransferRelease).
	class ReleaseLayer {
	public:
		explicit ReleaseLayer(HidLink& linkObject) : link(linkObject) {
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
		HidLink& link;
	};


	StateSink sink;
	bool portOpen = false;
	bool readHeld = false;
	std::size_t reportOffset = 0;
	std::size_t inputLength = ReportBytes;
	std::size_t outputLength = ReportBytes + 1;
	std::array<std::uint8_t, ReportBytes + 1> readBuffer{};
	std::array<std::uint8_t, ReportBytes + 1> writeBuffer{};
	TransferRelease<ReleaseLayer> release;
#if defined(_WIN32)
	HANDLE device = INVALID_HANDLE_VALUE;
	HANDLE readEvent = nullptr;
	HANDLE writeEvent = nullptr;
	OVERLAPPED readOverlapped{};
	OVERLAPPED writeOverlapped{};
	bool readPending = false;
	bool writePending = false;
#else
	int descriptor = -1;
#endif


public:
	HidLink() = default;


	HidLink(const HidLink&) = delete;
	HidLink& operator=(const HidLink&) = delete;


	~HidLink() {
		Shut(ReleaseWait::Unbounded);
	}


	// The usage page of a report descriptor: the first Usage Page item in it, which is the top-level collection of most devices. Zero when
	// the descriptor has none.
	static std::uint16_t UsagePageOf(std::span<const uint8> descriptor) {
		std::size_t index = 0;
		while (index < descriptor.size()) {
			std::uint8_t prefix = descriptor[index];
			if (prefix == 0xFE) {
				if (index + 1 >= descriptor.size()) {
					return 0;
				}

				index += 3 + static_cast<std::size_t>(descriptor[index + 1]);
				continue;
			}

			std::size_t dataSize = prefix & 0x03;
			if (dataSize == 3) {
				dataSize = 4;
			}

			std::uint8_t type = (prefix >> 2) & 0x03;
			std::uint8_t tag = (prefix >> 4) & 0x0F;
			if (type == 1 && tag == 0) {
				if (index + 1 + dataSize > descriptor.size()) {
					return 0;
				}

				std::uint16_t page = 0;
				for (std::size_t byte = 0; byte < dataSize && byte < 2; byte++) {
					page = static_cast<std::uint16_t>(page | (descriptor[index + 1 + byte] << (8 * byte)));
				}

				return page;
			}

			index += 1 + dataSize;
		}

		return 0;
	}


	// Writes a packet into an output report: the ID 0, then the report with the packet's length byte, the payload and zero padding. The
	// output must hold ReportBytes + 1 bytes. Returns the report's size (ReportBytes + 1), or invalidParameter when the packet is empty,
	// too long for the report, or the output is too small.
	static Result<std::size_t> PackOutput(std::span<const uint8> payload, std::span<uint8> output) {
		if (output.size() != ReportBytes + 1) {
			return ResultStatus::invalidParameter;
		}

		output[0] = 0;
		Result<std::size_t> packed = HidReportPack(payload, output.subspan(1, ReportBytes));
		if (!packed) {
			return packed;
		}

		return ReportBytes + 1;
	}


	// Opens the first device with the vendor ID, product ID and usage page (zero matches any usage page). Returns ok when the device is
	// open and up; an error otherwise, and the link stays closed. Returns busy while an earlier release still waits for its transfers.
	ResultStatus Open(std::uint16_t vendorId, std::uint16_t productId, std::uint16_t usagePage) {
		Close();
		if (release.Closing()) {
			return ResultStatus::busy;
		}

#if defined(_WIN32)
		GUID hidId{};
		HidD_GetHidGuid(&hidId);
		HDEVINFO set = SetupDiGetClassDevsW(&hidId, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (set == INVALID_HANDLE_VALUE) {
			return ResultStatus::ioError;
		}

		ResultStatus result = ResultStatus::notFound;
		for (DWORD index = 0;; index++) {
			SP_DEVICE_INTERFACE_DATA interfaceData{};
			interfaceData.cbSize = sizeof(interfaceData);
			if (!SetupDiEnumDeviceInterfaces(set, nullptr, &hidId, index, &interfaceData)) {
				break;
			}

			DWORD required = 0;
			SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, nullptr, 0, &required, nullptr);
			std::vector<BYTE> storage(required);
			SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
			detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
			if (!SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, detail, required, nullptr, nullptr)) {
				continue;
			}

			HANDLE candidate = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (candidate == INVALID_HANDLE_VALUE) {
				continue;
			}

			if (Matches(candidate, vendorId, productId, usagePage)) {
				device = candidate;
				result = ResultStatus::ok;
				break;
			}

			CloseHandle(candidate);
		}

		SetupDiDestroyDeviceInfoList(set);
		if (result != ResultStatus::ok) {
			return result;
		}

		readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		portOpen = true;
		sink.Report(true);
		StartRead();
		if (!portOpen) {
			return ResultStatus::ioError;
		}
#else
		for (int number = 0; number < 64 && descriptor < 0; number++) {
			std::string path = "/dev/hidraw" + std::to_string(number);
			int candidate = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
			if (candidate < 0) {
				continue;
			}

			hidraw_devinfo info{};
			bool ids = ioctl(candidate, HIDIOCGRAWINFO, &info) == 0 && static_cast<std::uint16_t>(info.vendor) == vendorId && static_cast<std::uint16_t>(info.product) == productId;
			if (ids && (usagePage == 0 || DescriptorUsagePage(candidate) == usagePage)) {
				descriptor = candidate;
				continue;
			}

			::close(candidate);
		}

		if (descriptor < 0) {
			return ResultStatus::notFound;
		}

		portOpen = true;
		sink.Report(true);
#endif
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


	// Feeds the report that arrived as a packet, when the receiver has room for it. Never waits.
	void Poll(Receiver receiver) {
		ReleaseLayer layer(*this);
		release.Finish(layer, ReleaseWait::None);
		if (!portOpen) {
			return;
		}

		if (!readHeld) {
			ReceiveStep();
		}

		if (portOpen && readHeld && receiver.Room() > 0) {
			FeedHeld(receiver);
			readHeld = false;
		}
	}


	// Writes a packet as an output report. Returns ok when the report is on its way, busy while the previous one is pending (the packet
	// is then sent again later), or another status when the packet is refused or the device failed. The previous report is checked for
	// completion before the packet is packed, so a report on its way is never overwritten.
	ResultStatus Send(std::span<const uint8> packet) {
		if (!portOpen) {
			return ResultStatus::noConnection;
		}

#if defined(_WIN32)
		if (writePending) {
			DWORD sent = 0;
			if (!GetOverlappedResult(device, &writeOverlapped, &sent, FALSE)) {
				if (GetLastError() == ERROR_IO_INCOMPLETE) {
					return ResultStatus::busy;
				}

				Abort();
				return ResultStatus::ioError;
			}

			writePending = false;
		}

		Result<std::size_t> packed = PackOutput(packet, writeBuffer);
		if (!packed) {
			return packed.Error();
		}

		std::size_t from = outputLength > ReportBytes ? 0 : 1;
		ResetEvent(writeEvent);
		writeOverlapped = OVERLAPPED{};
		writeOverlapped.hEvent = writeEvent;
		if (!WriteFile(device, writeBuffer.data() + from, static_cast<DWORD>(outputLength), nullptr, &writeOverlapped) && GetLastError() != ERROR_IO_PENDING) {
			Abort();
			return ResultStatus::ioError;
		}

		writePending = true;
		return ResultStatus::ok;
#else
		Result<std::size_t> packed = PackOutput(packet, writeBuffer);
		if (!packed) {
			return packed.Error();
		}

		ssize_t sent = write(descriptor, writeBuffer.data(), writeBuffer.size());
		if (sent == static_cast<ssize_t>(writeBuffer.size())) {
			return ResultStatus::ok;
		}

		if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
			return ResultStatus::busy;
		}

		Abort();
		return ResultStatus::ioError;
#endif
	}


private:
	void FeedHeld(Receiver receiver) {
		Result<std::span<const uint8>> payload = HidReportPayload(std::span<const uint8>(readBuffer.data() + reportOffset, ReportBytes));
		if (payload) {
			receiver.FeedPacket(payload.Value());
		}
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
		readHeld = false;
		ReleaseLayer layer(*this);
		release.Begin(layer);
		release.Finish(layer, wait);
	}


	// Cancels the transfers in flight. Their completion is seen later through TransfersInFlight.
	void CancelTransfers() {
#if defined(_WIN32)
		if (device != INVALID_HANDLE_VALUE && (readPending || writePending)) {
			CancelIoEx(device, nullptr);
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
		return false;
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
		static_cast<void>(milliseconds);
#endif
	}


	// Frees the device and its handles. Called only when no transfer is in flight.
	void FreeHandles() {
#if defined(_WIN32)
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
		if (descriptor >= 0) {
			::close(descriptor);
			descriptor = -1;
		}
#endif
	}


#if defined(_WIN32)
	// Whether a device with the IDs and the usage page is this candidate. It also takes the report lengths the OS uses: a report of
	// ReportBytes, or ReportBytes + 1 with the ID byte in front.
	bool Matches(HANDLE candidate, std::uint16_t vendorId, std::uint16_t productId, std::uint16_t usagePage) {
		HIDD_ATTRIBUTES attributes{};
		attributes.Size = sizeof(attributes);
		if (!HidD_GetAttributes(candidate, &attributes)) {
			return false;
		}

		if (attributes.VendorID != vendorId || attributes.ProductID != productId) {
			return false;
		}

		PHIDP_PREPARSED_DATA preparsed = nullptr;
		if (!HidD_GetPreparsedData(candidate, &preparsed)) {
			return false;
		}

		HIDP_CAPS caps{};
		NTSTATUS status = HidP_GetCaps(preparsed, &caps);
		HidD_FreePreparsedData(preparsed);
		if (status != HIDP_STATUS_SUCCESS) {
			return false;
		}

		if (usagePage != 0 && caps.UsagePage != usagePage) {
			return false;
		}

		std::size_t input = caps.InputReportByteLength;
		std::size_t output = caps.OutputReportByteLength;
		if ((input != ReportBytes && input != ReportBytes + 1) || (output != ReportBytes && output != ReportBytes + 1)) {
			return false;
		}

		inputLength = input;
		outputLength = output;
		reportOffset = input - ReportBytes;
		return true;
	}


	// Starts the overlapped read of the next input report into the link's buffer.
	void StartRead() {
		ResetEvent(readEvent);
		readOverlapped = OVERLAPPED{};
		readOverlapped.hEvent = readEvent;
		if (!ReadFile(device, readBuffer.data(), static_cast<DWORD>(inputLength), nullptr, &readOverlapped) && GetLastError() != ERROR_IO_PENDING) {
			Abort();
			return;
		}

		readPending = true;
	}


	// Finishes the pending read when it is done, or starts one when none is pending.
	void ReceiveStep() {
		if (!readPending) {
			StartRead();
			return;
		}

		DWORD got = 0;
		if (!GetOverlappedResult(device, &readOverlapped, &got, FALSE)) {
			if (GetLastError() != ERROR_IO_INCOMPLETE) {
				Abort();
			}

			return;
		}

		readPending = false;
		readHeld = static_cast<std::size_t>(got) == inputLength;
	}
#else
	// The usage page of an open hidraw node, from its report descriptor.
	static std::uint16_t DescriptorUsagePage(int candidate) {
		int size = 0;
		if (ioctl(candidate, HIDIOCGRDESCSIZE, &size) != 0 || size <= 0 || size > 4096) {
			return 0;
		}

		hidraw_report_descriptor descriptor{};
		descriptor.size = static_cast<__u32>(size);
		if (ioctl(candidate, HIDIOCGRDESC, &descriptor) != 0) {
			return 0;
		}

		return UsagePageOf(std::span<const uint8>(descriptor.value, static_cast<std::size_t>(size)));
	}


	// Reads the next report of the device into the link's buffer, when no report is held. A report that does not come is not an error.
	void ReceiveStep() {
		ssize_t got = read(descriptor, readBuffer.data(), ReportBytes);
		if (got == static_cast<ssize_t>(ReportBytes)) {
			readHeld = true;
			return;
		}

		if (got < 0 && !(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
			Abort();
		}
	}
#endif
};

}
