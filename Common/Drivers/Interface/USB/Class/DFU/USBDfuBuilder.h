#pragma once
#include <array>
#include <cstddef>
#include <string_view>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// Class builder of one DFU interface (USBConfigurationBuilder::Function()). A DFU interface has no endpoints: its requests all go
// to the control pipe. The run-time interface (Runtime()) answers DFU_DETACH and nothing else. The DFU mode interface (Mode())
// takes the download and upload requests of the state machine in DfuFunction.
//
// The interface is class 0xFE, subclass 0x01, protocol 0x01 in run time and 0x02 in DFU mode. Its functional descriptor carries
// bmAttributes (CanDnload, CanUpload, ManifestationTolerant, WillDetach), wDetachTimeOut in milliseconds, wTransferSize in
// bytes and bcdDFUVersion 0x0110.
//
// Rules, checked when the chain is evaluated:
//   - exactly one of Runtime() and Mode(), and a DFU mode interface can download or upload (CanDnload() or CanUpload());
//   - TransferSize() is required and is 1 to 4096 bytes;
//   - a run-time interface without WillDetach() needs a detach timeout of at least 1 ms, because the host's reset must come within
//     it. wDetachTimeOut belongs to DFU_DETACH, so a mode interface does not need it;
//   - Region() is for a mode interface only, one to four of them, and is not given together with Name().
//
// Memory regions: each Region() is an alternate setting of the mode interface (DFU 1.1 section 4.2.3). The first region is
// alternate 0, the next one alternate 1, and so on, each with the region's name as its interface string. The host selects a
// region with SET_INTERFACE (dfu-util -a N); DfuFunction starts the transfer on that alternate.
//
// WinUsb() binds WinUSB to the interface through the MS OS 2.0 descriptors (MsOs20VendorCode() on the device). dfu-util on
// Windows needs it: the host cannot open the interface without a driver that is not the one that ships with Windows.
class USBDfuBuilder {
private:
	bool kindSet = false;
	bool mode = false;
	bool transferSizeSet = false;
	uint16 transferSize = 0;
	bool canDnload = false;
	bool canUpload = false;
	bool manifestationTolerant = false;
	bool willDetach = false;
	uint16 detachTimeoutMs = 0;
	std::string_view name;
	std::array<std::string_view, USBMaxFunctionStrings> regions{};
	std::size_t regionCount = 0;
	bool winUsb = false;
	std::string_view guid;


public:
	// The run-time interface: DFU_DETACH only. The application restarts into its loader on the detach request.
	constexpr USBDfuBuilder& Runtime() {
		Require<"Runtime() and Mode() are given once">(!kindSet);
		kindSet = true;
		mode = false;
		return *this;
	}


	// The DFU mode interface. Its functions are the download, upload, status and abort requests.
	constexpr USBDfuBuilder& Mode() {
		Require<"Runtime() and Mode() are given once">(!kindSet);
		kindSet = true;
		mode = true;
		return *this;
	}


	// bitCanDnload: the device accepts DFU_DNLOAD blocks.
	constexpr USBDfuBuilder& CanDnload() {
		canDnload = true;
		return *this;
	}


	// bitCanUpload: the device answers DFU_UPLOAD blocks.
	constexpr USBDfuBuilder& CanUpload() {
		canUpload = true;
		return *this;
	}


	// bitManifestationTolerant: the device still answers on the bus after the manifestation phase, so the host does not reset it.
	constexpr USBDfuBuilder& ManifestationTolerant() {
		manifestationTolerant = true;
		return *this;
	}


	// bitWillDetach: the device performs its own detach and attach after DFU_DETACH (run time), or after the manifestation of a mode
	// device that does not stay on the bus (not ManifestationTolerant()); the host must not send a USB reset then. The application
	// restarts the device when the detach delegate asks for it.
	constexpr USBDfuBuilder& WillDetach() {
		willDetach = true;
		return *this;
	}


	// wDetachTimeOut: the time the device waits for the host's USB reset after DFU_DETACH, in milliseconds. The host's own
	// wTimeout is clamped to it. Only the run-time interface uses it; a mode interface may leave it out.
	constexpr USBDfuBuilder& DetachTimeoutMs(uint16 milliseconds) {
		detachTimeoutMs = milliseconds;
		return *this;
	}


	// wTransferSize: the largest DFU_DNLOAD block and DFU_UPLOAD answer, in bytes. Required, 1 to 4096.
	constexpr USBDfuBuilder& TransferSize(uint16 bytes) {
		Require<"wTransferSize is 1 to 4096 bytes">(bytes >= 1 && bytes <= DfuMaxTransferSize);
		transferSize = bytes;
		transferSizeSet = true;
		return *this;
	}


	// The interface string of an interface with one memory region. A mode interface with several regions names each one with Region().
	constexpr USBDfuBuilder& Name(std::string_view text) {
		name = text;
		return *this;
	}


	// One memory region of the DFU mode interface, named by its interface string (for example "Flash" or "EEPROM"). Each region is
	// an alternate setting: the first is alternate 0, the next alternate 1, in the order the calls are made. A region is how the
	// host tells the memory apart: dfu-util -a N selects the region N, and the target's Begin() receives N. At most four regions,
	// the string limit of one function.
	constexpr USBDfuBuilder& Region(std::string_view text) {
		Require<"a DFU mode interface has at most 4 regions">(regionCount < USBMaxFunctionStrings);
		regions[regionCount] = text;
		regionCount++;
		return *this;
	}


	// Binds WinUSB to the interface. Needs MsOs20VendorCode() on the device.
	constexpr USBDfuBuilder& WinUsb() {
		winUsb = true;
		return *this;
	}


	// Binds WinUSB and names the interface GUID that applications open the device with, for example "{...}" in registry form.
	constexpr USBDfuBuilder& WinUsb(std::string_view interfaceGuid) {
		winUsb = true;
		guid = interfaceGuid;
		return *this;
	}


	constexpr std::size_t InterfaceCount() const {
		Require<"a DFU interface is Runtime() or Mode()">(kindSet);
		Require<"TransferSize() is required">(transferSizeSet);
		Require<"a DFU mode interface downloads or uploads: call CanDnload() or CanUpload()">(!mode || canDnload || canUpload);
		Require<"a device without WillDetach() needs DetachTimeoutMs() of at least 1">(mode || willDetach || detachTimeoutMs >= 1);
		Require<"Region() is for a DFU mode interface">(regionCount == 0 || mode);
		Require<"Name() and Region() are not both given">(regionCount == 0 || name.empty());
		return 1;
	}


	constexpr std::size_t EndpointCount() const {
		return 0;
	}


	// A DFU interface has no endpoints, so the configuration builder never asks for one.
	constexpr USBEndpointRequest Endpoint(std::size_t index) const {
		static_cast<void>(index);
		return USBEndpointRequest{};
	}


	// One string per region, or the interface name when there are no regions.
	constexpr std::size_t StringCount() const {
		if (regionCount > 0) {
			return regionCount;
		}

		return name.empty() ? 0 : 1;
	}


	constexpr std::string_view String(std::size_t index) const {
		if (regionCount > 0) {
			return regions[index];
		}

		return name;
	}


	constexpr uint8 FunctionClass() const {
		return static_cast<uint8>(USBClassCode::ApplicationSpecific);
	}


	constexpr uint8 FunctionSubClass() const {
		return DfuInterfaceSubClass;
	}


	constexpr uint8 FunctionProtocol() const {
		return mode ? DfuModeProtocol : DfuRuntimeProtocol;
	}


	constexpr bool HasWinUsb() const {
		return winUsb;
	}


	constexpr std::string_view WinUsbGuid() const {
		return guid;
	}


	// One interface descriptor per region, alternate setting 0 first, then the functional descriptor once after all of them: DFU 1.1
	// section 4.2 lists a single interface descriptor, including the descriptors of its alternate settings, followed by a single
	// functional descriptor.
	constexpr void Emit(USBFunctionEmitter& emitter) const {
		uint8 interfaceClass = static_cast<uint8>(USBClassCode::ApplicationSpecific);
		if (regionCount == 0) {
			emitter.Interface(0, interfaceClass, DfuInterfaceSubClass, FunctionProtocol(), name.empty() ? USBNoString : 0);
		} else {
			emitter.Interface(0, interfaceClass, DfuInterfaceSubClass, FunctionProtocol(), 0);
			for (std::size_t index = 1; index < regionCount; index++) {
				emitter.Alternate(0, static_cast<uint8>(index), interfaceClass, DfuInterfaceSubClass, FunctionProtocol(), index);
			}
		}

		std::array<uint8, 9> functional = MakeUSBDfuFunctionalDescriptor(Attributes(), detachTimeoutMs, transferSize);
		emitter.Raw(functional);
	}


private:
	constexpr uint8 Attributes() const {
		uint8 attributes = 0;
		if (canDnload) {
			attributes |= DfuAttributeCanDnload;
		}

		if (canUpload) {
			attributes |= DfuAttributeCanUpload;
		}

		if (manifestationTolerant) {
			attributes |= DfuAttributeManifestationTolerant;
		}

		if (willDetach) {
			attributes |= DfuAttributeWillDetach;
		}

		return attributes;
	}
};
