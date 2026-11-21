#include "LibUsbBackend.h"

#include <sys/time.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "DeviceSelection.h"
#include "Text.h"

namespace {

// How long cancelled transfers may take to report back. libusb ends a cancelled transfer on the event loop, and a
// transfer never runs past BulkTimeoutMs, so three timeouts is a wide margin.
constexpr std::uint32_t CancelWaitMs = 3 * BulkTimeoutMs;

// Bookkeeping of one RunBulk call, shared by all of its slots.
struct LibUsbRun {
	std::uint32_t inFlight = 0;
	bool failed = false;
	std::uint8_t failedEndpoint = 0;
	libusb_transfer_status failedStatus = LIBUSB_TRANSFER_COMPLETED;
	int failedActual = 0;
	int failedRequested = 0;
};

// One transfer slot. libusb hands its address back to the completion callback.
struct LibUsbSlot {
	libusb_transfer* transfer = nullptr;
	BulkLane* lane = nullptr;
	LibUsbRun* run = nullptr;
	std::size_t slot = 0;
	bool inFlight = false;
};

// The slots of one lane (one endpoint) in a run.
struct LibUsbBinding {
	BulkLane* lane = nullptr;
	std::uint8_t endpoint = 0;
	std::vector<LibUsbSlot> slots;
};


const char* TransferStatusName(libusb_transfer_status status) {
	switch (status) {
		case LIBUSB_TRANSFER_COMPLETED:
			return "completed";
		case LIBUSB_TRANSFER_ERROR:
			return "I/O error";
		case LIBUSB_TRANSFER_TIMED_OUT:
			return "timed out (the device stopped answering)";
		case LIBUSB_TRANSFER_CANCELLED:
			return "cancelled";
		case LIBUSB_TRANSFER_STALL:
			return "endpoint stalled";
		case LIBUSB_TRANSFER_NO_DEVICE:
			return "device disconnected";
		case LIBUSB_TRANSFER_OVERFLOW:
			return "device sent more data than requested";
	}

	return "unknown status";
}


// Runs on the thread that calls libusb_handle_events. Only records the outcome; the main loop does the checks.
void LIBUSB_CALL OnBulkTransferDone(libusb_transfer* transfer) {
	LibUsbSlot* slotState = static_cast<LibUsbSlot*>(transfer->user_data);
	LibUsbRun* run = slotState->run;
	slotState->inFlight = false;
	run->inFlight--;

	if (transfer->status == LIBUSB_TRANSFER_COMPLETED && transfer->actual_length == transfer->length) {
		slotState->lane->CompleteSubmit(slotState->slot);
		return;
	}

	// Only the first failure is kept. Cancellations that follow it are expected and must not replace it.
	if (!run->failed) {
		run->failed = true;
		run->failedEndpoint = transfer->endpoint;
		run->failedStatus = transfer->status;
		run->failedActual = transfer->actual_length;
		run->failedRequested = transfer->length;
	}
}


void AddBinding(std::vector<LibUsbBinding>& bindings, BulkLane* lane, std::uint8_t endpoint, LibUsbRun& run) {
	if (lane == nullptr) {
		return;
	}

	LibUsbBinding binding;
	binding.lane = lane;
	binding.endpoint = endpoint;
	binding.slots.resize(lane->GetSlotCount());
	for (std::size_t index = 0; index < binding.slots.size(); index++) {
		LibUsbSlot& slot = binding.slots[index];
		slot.transfer = libusb_alloc_transfer(0);
		slot.lane = lane;
		slot.run = &run;
		slot.slot = index;
	}

	// reserve() is called by the caller, so this move keeps the slot storage at the same address.
	bindings.push_back(std::move(binding));
}


// Starts transfers on one lane while it has bytes left and a free slot. Sets run.failed on the first error.
void SubmitTransfers(libusb_device_handle* deviceHandle, LibUsbBinding& binding, LibUsbRun& run, std::string& error) {
	while (!run.failed && binding.lane->CanSubmit()) {
		BulkTransfer transfer;
		binding.lane->BeginSubmit(transfer);

		LibUsbSlot& slot = binding.slots[transfer.slot];
		if (slot.transfer == nullptr) {
			error = "libusb_alloc_transfer failed: out of memory";
			run.failed = true;
			return;
		}

		libusb_fill_bulk_transfer(
			slot.transfer,
			deviceHandle,
			binding.endpoint,
			transfer.data,
			static_cast<int>(transfer.length),
			OnBulkTransferDone,
			&slot,
			BulkTimeoutMs);

		slot.inFlight = true;
		run.inFlight++;
		int result = libusb_submit_transfer(slot.transfer);
		if (result < 0) {
			slot.inFlight = false;
			run.inFlight--;
			error = "submit failed on endpoint " + std::to_string(binding.endpoint) + ": " + libusb_error_name(result);
			run.failed = true;
			return;
		}
	}
}


bool AllFinished(const std::vector<LibUsbBinding>& bindings) {
	for (const LibUsbBinding& binding : bindings) {
		if (!binding.lane->IsFinished()) {
			return false;
		}
	}

	return true;
}


void CancelInFlight(std::vector<LibUsbBinding>& bindings) {
	for (LibUsbBinding& binding : bindings) {
		for (LibUsbSlot& slot : binding.slots) {
			if (slot.inFlight) {
				libusb_cancel_transfer(slot.transfer);
			}
		}
	}
}


// Waits until every cancelled transfer has reported back. If one never does, libusb still holds its slot and buffer,
// and returning would free them under it. The process stops instead, so no callback ever writes into freed memory.
void WaitForCancelled(libusb_context* context, const LibUsbRun& run, timeval& eventTimeout) {
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::milliseconds(CancelWaitMs);
	while (run.inFlight > 0) {
		if (std::chrono::steady_clock::now() > deadline) {
			std::fputs(
				"usbbench: cancelled USB transfers did not report back; stopping so no buffer is freed while libusb uses it\n",
				stderr);
			std::abort();
		}

		libusb_handle_events_timeout_completed(context, &eventTimeout, nullptr);
	}
}


void FreeIdleTransfers(std::vector<LibUsbBinding>& bindings) {
	for (LibUsbBinding& binding : bindings) {
		for (LibUsbSlot& slot : binding.slots) {
			// A transfer still in flight is not freed; libusb would write into freed memory when it completes.
			if (slot.transfer != nullptr && !slot.inFlight) {
				libusb_free_transfer(slot.transfer);
				slot.transfer = nullptr;
			}
		}
	}
}


std::string DescribeFailure(const LibUsbRun& run) {
	std::string text;
	AppendFormat(
		text,
		"transfer on endpoint 0x%02X failed: %s (%d of %d bytes)",
		static_cast<unsigned int>(run.failedEndpoint),
		TransferStatusName(run.failedStatus),
		run.failedActual,
		run.failedRequested);
	return text;
}


const char* TransferTypeName(std::uint8_t attributes) {
	switch (attributes & LIBUSB_TRANSFER_TYPE_MASK) {
		case LIBUSB_TRANSFER_TYPE_CONTROL:
			return "control";
		case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
			return "isochronous";
		case LIBUSB_TRANSFER_TYPE_BULK:
			return "bulk";
		case LIBUSB_TRANSFER_TYPE_INTERRUPT:
			return "interrupt";
	}

	return "unknown";
}


std::string ReadString(libusb_device_handle* deviceHandle, std::uint8_t index) {
	std::string text;
	if (index == 0) {
		return text;
	}

	unsigned char buffer[256] {};
	int length = libusb_get_string_descriptor_ascii(deviceHandle, index, buffer, static_cast<int>(sizeof(buffer)));
	if (length > 0) {
		text.assign(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(length));
	}

	return text;
}


// The bus and port path of a device. It tells apart two boards that report the same serial number.
std::string DeviceLocation(libusb_device* device) {
	std::string text;
	AppendFormat(text, "bus %u", static_cast<unsigned int>(libusb_get_bus_number(device)));

	std::uint8_t ports[8] {};
	int count = libusb_get_port_numbers(device, ports, static_cast<int>(sizeof(ports)));
	for (int index = 0; index < count; index++) {
		if (index == 0) {
			AppendFormat(text, " port %u", static_cast<unsigned int>(ports[index]));
		} else {
			AppendFormat(text, ".%u", static_cast<unsigned int>(ports[index]));
		}
	}

	return text;
}

}


LibUsbBackend::~LibUsbBackend() {
	Close();
	if (context != nullptr) {
		libusb_exit(context);
		context = nullptr;
	}
}


bool LibUsbBackend::Enumerate(std::vector<UsbDeviceInfo>& devices, std::string& error) {
	if (!EnsureContext(error)) {
		return false;
	}

	devices.clear();
	libusb_device** libusbDevices = nullptr;
	std::ptrdiff_t deviceCount = static_cast<std::ptrdiff_t>(libusb_get_device_list(context, &libusbDevices));
	if (deviceCount < 0) {
		error = "libusb_get_device_list failed";
		return false;
	}

	for (std::ptrdiff_t index = 0; index < deviceCount; index++) {
		libusb_device_descriptor descriptor {};
		if (libusb_get_device_descriptor(libusbDevices[index], &descriptor) < 0) {
			continue;
		}

		if (descriptor.idVendor != BenchProtocol::VendorId || descriptor.idProduct != BenchProtocol::ProductId) {
			continue;
		}

		UsbDeviceInfo info;
		info.location = DeviceLocation(libusbDevices[index]);

		libusb_device_handle* opened = nullptr;
		int result = libusb_open(libusbDevices[index], &opened);
		if (result < 0) {
			info.problem = std::string("cannot open the device: ") + libusb_error_name(result);
		} else {
			if (descriptor.iSerialNumber == 0) {
				info.problem = "the device reports no serial number";
			} else {
				info.serial = ReadString(opened, descriptor.iSerialNumber);
				if (info.serial.empty()) {
					info.problem = "the serial string could not be read";
				}
			}

			libusb_close(opened);
		}

		devices.push_back(info);
	}

	libusb_free_device_list(libusbDevices, 1);
	return true;
}


bool LibUsbBackend::Open(const std::string& serial, std::string& error) {
	Close();
	if (!EnsureContext(error)) {
		return false;
	}

	libusb_device** devices = nullptr;
	std::ptrdiff_t deviceCount = static_cast<std::ptrdiff_t>(libusb_get_device_list(context, &devices));
	if (deviceCount < 0) {
		error = "libusb_get_device_list failed";
		return false;
	}

	bool found = false;
	OpenFailures failures;
	failures.serial = serial;
	for (std::ptrdiff_t index = 0; index < deviceCount && !found; index++) {
		libusb_device_descriptor descriptor {};
		if (libusb_get_device_descriptor(devices[index], &descriptor) < 0) {
			continue;
		}

		if (descriptor.idVendor != BenchProtocol::VendorId || descriptor.idProduct != BenchProtocol::ProductId) {
			continue;
		}

		libusb_device_handle* opened = nullptr;
		int result = libusb_open(devices[index], &opened);
		if (result < 0) {
			// Without an open handle the serial cannot be read, so this device may be the one that was asked for.
			failures.unreadable++;
			failures.readError = std::string("cannot open the benchmark device: ") + libusb_error_name(result);
			continue;
		}

		// Several boards share the IDs, so the serial number decides which one stays open.
		if (!serial.empty()) {
			std::string candidate = ReadString(opened, descriptor.iSerialNumber);
			if (candidate.empty()) {
				libusb_close(opened);
				failures.unreadable++;
				failures.readError = "the serial string could not be read";
				continue;
			}

			if (!SameSerial(candidate, serial)) {
				libusb_close(opened);
				continue;
			}
		}

		result = libusb_claim_interface(opened, BenchProtocol::InterfaceNumber);
		if (result < 0) {
			// Without a claim there is no access to the endpoints; on Windows this usually means WinUSB is not bound.
			failures.matchError = std::string("cannot claim interface 0: ") + libusb_error_name(result)
				+ " (on Windows the interface must be bound to WinUSB)";
			libusb_close(opened);
			continue;
		}

		handle = opened;
		found = true;
	}

	libusb_free_device_list(devices, 1);

	if (!found) {
		error = OpenFailureText(failures);
		return false;
	}

	return true;
}


void LibUsbBackend::Close() {
	if (handle != nullptr) {
		libusb_release_interface(handle, BenchProtocol::InterfaceNumber);
		libusb_close(handle);
		handle = nullptr;
	}
}


bool LibUsbBackend::Describe(std::string& text, std::string& error) {
	if (handle == nullptr) {
		error = "the device is not open";
		return false;
	}

	libusb_device* device = libusb_get_device(handle);
	libusb_device_descriptor descriptor {};
	if (libusb_get_device_descriptor(device, &descriptor) < 0) {
		error = "libusb_get_device_descriptor failed";
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
		"Class         %02X:%02X:%02X, control packet %u bytes, %u configuration(s)\n",
		static_cast<unsigned int>(descriptor.bDeviceClass),
		static_cast<unsigned int>(descriptor.bDeviceSubClass),
		static_cast<unsigned int>(descriptor.bDeviceProtocol),
		static_cast<unsigned int>(descriptor.bMaxPacketSize0),
		static_cast<unsigned int>(descriptor.bNumConfigurations));
	AppendFormat(
		text,
		"Strings       manufacturer \"%s\", product \"%s\", serial \"%s\"\n",
		ReadString(handle, descriptor.iManufacturer).c_str(),
		ReadString(handle, descriptor.iProduct).c_str(),
		ReadString(handle, descriptor.iSerialNumber).c_str());

	libusb_config_descriptor* config = nullptr;
	if (libusb_get_active_config_descriptor(device, &config) < 0) {
		error = "libusb_get_active_config_descriptor failed";
		return false;
	}

	AppendFormat(
		text,
		"Configuration %u: %u interface(s), %u bytes, max power %u mA, attributes 0x%02X\n",
		static_cast<unsigned int>(config->bConfigurationValue),
		static_cast<unsigned int>(config->bNumInterfaces),
		static_cast<unsigned int>(config->wTotalLength),
		static_cast<unsigned int>(config->MaxPower) * 2,
		static_cast<unsigned int>(config->bmAttributes));

	for (std::uint8_t interfaceIndex = 0; interfaceIndex < config->bNumInterfaces; interfaceIndex++) {
		const libusb_interface& iface = config->interface[interfaceIndex];
		for (int altIndex = 0; altIndex < iface.num_altsetting; altIndex++) {
			const libusb_interface_descriptor& setting = iface.altsetting[altIndex];
			AppendFormat(
				text,
				"Interface %u alt %u: class %02X:%02X:%02X, %u endpoint(s)\n",
				static_cast<unsigned int>(setting.bInterfaceNumber),
				static_cast<unsigned int>(setting.bAlternateSetting),
				static_cast<unsigned int>(setting.bInterfaceClass),
				static_cast<unsigned int>(setting.bInterfaceSubClass),
				static_cast<unsigned int>(setting.bInterfaceProtocol),
				static_cast<unsigned int>(setting.bNumEndpoints));

			for (std::uint8_t endpointIndex = 0; endpointIndex < setting.bNumEndpoints; endpointIndex++) {
				const libusb_endpoint_descriptor& endpoint = setting.endpoint[endpointIndex];
				AppendFormat(
					text,
					"  Endpoint 0x%02X %s, max packet %u bytes\n",
					static_cast<unsigned int>(endpoint.bEndpointAddress),
					TransferTypeName(endpoint.bmAttributes),
					static_cast<unsigned int>(endpoint.wMaxPacketSize));
			}
		}
	}

	libusb_free_config_descriptor(config);
	return true;
}


bool LibUsbBackend::ControlIn(
	BenchProtocol::Request request,
	std::uint16_t value,
	std::span<std::uint8_t> data,
	std::size_t& received,
	std::string& error)
{
	if (handle == nullptr) {
		error = "the device is not open";
		return false;
	}

	int result = libusb_control_transfer(
		handle,
		VendorRequestIn,
		static_cast<std::uint8_t>(request),
		value,
		BenchProtocol::InterfaceNumber,
		data.data(),
		static_cast<std::uint16_t>(data.size()),
		ControlTimeoutMs);
	if (result < 0) {
		error = std::string("control request failed: ") + libusb_error_name(result);
		return false;
	}

	received = static_cast<std::size_t>(result);
	return true;
}


bool LibUsbBackend::ControlOut(BenchProtocol::Request request, std::uint16_t value, std::string& error) {
	if (handle == nullptr) {
		error = "the device is not open";
		return false;
	}

	int result = libusb_control_transfer(
		handle,
		VendorRequestOut,
		static_cast<std::uint8_t>(request),
		value,
		BenchProtocol::InterfaceNumber,
		nullptr,
		0,
		ControlTimeoutMs);
	if (result < 0) {
		error = std::string("control request failed: ") + libusb_error_name(result);
		return false;
	}

	return true;
}


bool LibUsbBackend::DrainBulkIn(std::string& error) {
	if (handle == nullptr) {
		error = "the device is not open";
		return false;
	}

	std::vector<std::uint8_t> buffer(DrainChunkBytes);
	std::size_t drained = 0;
	while (true) {
		int actual = 0;
		int result = libusb_bulk_transfer(
			handle,
			BenchProtocol::BulkInAddress,
			buffer.data(),
			static_cast<int>(buffer.size()),
			&actual,
			DrainQuietMs);
		if (result == LIBUSB_ERROR_TIMEOUT) {
			return true;
		}

		if (result < 0) {
			error = std::string("drain read failed: ") + libusb_error_name(result);
			return false;
		}

		drained += static_cast<std::size_t>(actual);
		if (drained > MaxDrainBytes) {
			error = "the device keeps sending stream data after it was set to idle";
			return false;
		}
	}
}


bool LibUsbBackend::RunBulk(BulkLane* inLane, BulkLane* outLane, std::string& error) {
	if (handle == nullptr) {
		error = "the device is not open";
		return false;
	}

	LibUsbRun run;
	std::vector<LibUsbBinding> bindings;
	bindings.reserve(2);
	AddBinding(bindings, inLane, BenchProtocol::BulkInAddress, run);
	AddBinding(bindings, outLane, BenchProtocol::BulkOutAddress, run);

	timeval eventTimeout {};
	eventTimeout.tv_sec = 0;
	eventTimeout.tv_usec = 100000;

	while (!run.failed) {
		// Retired slots are refilled in the same pass, so a finished transfer never leaves its slot idle for a wait.
		for (LibUsbBinding& binding : bindings) {
			binding.lane->VerifyCompleted();
		}

		for (LibUsbBinding& binding : bindings) {
			SubmitTransfers(handle, binding, run, error);
		}

		if (run.failed || AllFinished(bindings)) {
			break;
		}

		int result = libusb_handle_events_timeout_completed(context, &eventTimeout, nullptr);
		if (result < 0) {
			error = std::string("libusb_handle_events failed: ") + libusb_error_name(result);
			run.failed = true;
		}
	}

	if (run.failed && error.empty()) {
		error = DescribeFailure(run);
	}

	if (run.failed) {
		// No transfer may outlive this call: the slots and the lane buffers belong to it.
		CancelInFlight(bindings);
		WaitForCancelled(context, run, eventTimeout);
	}

	FreeIdleTransfers(bindings);
	return !run.failed;
}


bool LibUsbBackend::EnsureContext(std::string& error) {
	if (context != nullptr) {
		return true;
	}

	int result = libusb_init(&context);
	if (result < 0) {
		context = nullptr;
		error = std::string("libusb_init failed: ") + libusb_error_name(result);
		return false;
	}

	return true;
}
