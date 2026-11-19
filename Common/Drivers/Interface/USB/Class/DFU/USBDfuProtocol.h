#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// Device Firmware Upgrade 1.1: the interface codes, the class requests, the states and status codes of the state machine
// (DFU 1.1 section 3, section 4, section 6 and appendix A), and the DFU functional descriptor (section 4.1.3).


// Interface subclass of every DFU interface, and the protocol of the run-time interface and of the DFU mode interface.
constexpr uint8 DfuInterfaceSubClass = 0x01;
constexpr uint8 DfuRuntimeProtocol = 0x01;
constexpr uint8 DfuModeProtocol = 0x02;

// bcdDFUVersion of the functional descriptor.
constexpr uint16 DfuVersion = 0x0110;

// Largest wTransferSize the builder accepts. A control-write block is held in the function's buffer, which is a template
// argument of DfuFunction, and a block larger than the control transfers of a full-speed device is not useful.
constexpr uint16 DfuMaxTransferSize = 4096;

// bmAttributes bits of the functional descriptor.
constexpr uint8 DfuAttributeCanDnload = 0x01;
constexpr uint8 DfuAttributeCanUpload = 0x02;
constexpr uint8 DfuAttributeManifestationTolerant = 0x04;
constexpr uint8 DfuAttributeWillDetach = 0x08;

constexpr uint8 DfuFunctionalDescriptorType = 0x21;
constexpr std::size_t DfuFunctionalDescriptorLength = 9;

// bRequest values of the class requests (DFU 1.1 table 3.2). Every request goes to the interface.
enum class DfuRequest : uint8 {
	Detach = 0,
	Dnload = 1,
	Upload = 2,
	GetStatus = 3,
	ClrStatus = 4,
	GetState = 5,
	Abort = 6
};


// The device states (DFU 1.1 section 6 and appendix A.1). A DFU mode function is never in the two application states.
enum class DfuState : uint8 {
	appIDLE = 0,
	appDETACH = 1,
	dfuIDLE = 2,
	dfuDNLOAD_SYNC = 3,
	dfuDNBUSY = 4,
	dfuDNLOAD_IDLE = 5,
	dfuMANIFEST_SYNC = 6,
	dfuMANIFEST = 7,
	dfuMANIFEST_WAIT_RESET = 8,
	dfuUPLOAD_IDLE = 9,
	dfuERROR = 10
};


// The bStatus values of DFU_GETSTATUS (DFU 1.1 section 6.1.2). Each error is the status the device reports while it is in
// dfuERROR, until DFU_CLRSTATUS.
enum class DfuStatus : uint8 {
	OK = 0x00,
	errTARGET = 0x01,
	errFILE = 0x02,
	errWRITE = 0x03,
	errERASE = 0x04,
	errCHECK_ERASED = 0x05,
	errPROG = 0x06,
	errVERIFY = 0x07,
	errADDRESS = 0x08,
	errNOTDONE = 0x09,
	errFIRMWARE = 0x0A,
	errVENDOR = 0x0B,
	errUSBR = 0x0C,
	errPOR = 0x0D,
	errUNKNOWN = 0x0E,
	errSTALLEDPKT = 0x0F
};


// What a target is asked to time: a write block (erase included) or the manifestation. Its poll timeout is reported in
// DFU_GETSTATUS while the host waits.
enum class DfuOperation : uint8 {
	Write = 0,
	Manifest = 1
};


// The direction each request is sent in (DFU 1.1 table 3.1). A request sent the other way is treated as an unknown request.
constexpr USBDirection DfuRequestDirection(DfuRequest request) {
	if (request == DfuRequest::Upload || request == DfuRequest::GetStatus || request == DfuRequest::GetState) {
		return USBDirection::In;
	}

	return USBDirection::Out;
}


// The functional descriptor of DFU 1.1 section 4.1.3: bLength 9, bDescriptorType 0x21, bmAttributes, wDetachTimeOut (ms),
// wTransferSize (bytes) and bcdDFUVersion 0x0110. The same bytes serve the run-time and the DFU mode interface.
constexpr std::array<uint8, 9> MakeUSBDfuFunctionalDescriptor(uint8 attributes, uint16 detachTimeoutMs, uint16 transferSize) {
	std::array<uint8, 9> bytes{};
	bytes[0] = 9;
	bytes[1] = DfuFunctionalDescriptorType;
	bytes[2] = attributes;
	USBDescriptorPut16(bytes, 3, detachTimeoutMs);
	USBDescriptorPut16(bytes, 5, transferSize);
	USBDescriptorPut16(bytes, 7, DfuVersion);
	return bytes;
}


// The fields of the functional descriptor a function reads from its layout. found is false when the layout has none.
struct USBDfuFunctional {
	bool found = false;
	uint8 attributes = 0;
	uint16 detachTimeoutMs = 0;
	uint16 transferSize = 0;
};


// Finds the DFU functional descriptor in a function's descriptor slice and reads its fields. The walk follows bLength, so the
// position of the descriptor in the slice does not matter, and a malformed length stops the walk.
constexpr USBDfuFunctional ParseUSBDfuFunctional(std::span<const uint8> descriptors) {
	USBDfuFunctional functional;
	std::size_t offset = 0;
	while (offset + 2 <= descriptors.size()) {
		std::size_t length = descriptors[offset];
		if (length < 2 || offset + length > descriptors.size()) {
			break;
		}

		if (descriptors[offset + 1] == DfuFunctionalDescriptorType && length == DfuFunctionalDescriptorLength) {
			functional.found = true;
			functional.attributes = descriptors[offset + 2];
			functional.detachTimeoutMs = static_cast<uint16>(descriptors[offset + 3] | (descriptors[offset + 4] << 8));
			functional.transferSize = static_cast<uint16>(descriptors[offset + 5] | (descriptors[offset + 6] << 8));
			return functional;
		}

		offset += length;
	}

	return functional;
}


// The six bytes of DFU_GETSTATUS: bStatus, bwPollTimeout (3 bytes, little-endian, clamped to 0xFFFFFF), bState and iString 0.
constexpr std::array<uint8, 6> MakeUSBDfuStatusResponse(DfuStatus status, uint32 pollTimeoutMs, DfuState state) {
	std::array<uint8, 6> bytes{};
	uint32 poll = pollTimeoutMs > 0xFFFFFF ? 0xFFFFFF : pollTimeoutMs;
	bytes[0] = static_cast<uint8>(status);
	bytes[1] = static_cast<uint8>(poll & 0xFF);
	bytes[2] = static_cast<uint8>((poll >> 8) & 0xFF);
	bytes[3] = static_cast<uint8>((poll >> 16) & 0xFF);
	bytes[4] = static_cast<uint8>(state);
	bytes[5] = 0;
	return bytes;
}
