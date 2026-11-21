#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcEndpointBuilder.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// Class builder of a CDC-ACM function (USBConfigurationBuilder::Function()): a serial port over the abstract control model. It
// has two interfaces, the communication interface (class 0x02, subclass 0x02, protocol 0x00) and the data interface (class 0x0A),
// and so an interface association over both. The Windows 10 and later usbser driver, the Linux cdc_acm driver and the macOS driver
// bind their in-box drivers to it: no INF and no kernel extension.
//
// Layout of the interfaces, as the configuration writes them:
//   communication interface 0: Header (bcdCDC 0x0110), Call Management (bmCapabilities 0, bDataInterface 1),
//                              Abstract Control Management (bmCapabilities 0x02), Union (control 0, subordinate 1),
//                              then the notification endpoint;
//   data interface 1:          the bulk IN and bulk OUT endpoints, with no alternate setting.
//
// The endpoints, their addresses and their order are the ones USBCdcEndpointBuilder describes, and CdcAcmFunction reads them in that
// order. The notification endpoint is 16 bytes, as the specification's SERIAL_STATE needs 10 and the 16-byte packet is the usual size;
// its interval defaults to 16 ms.
//
// Name() is the communication interface's string. The data interface has none.
class USBCdcAcmBuilder : public USBCdcEndpointBuilder<USBCdcAcmBuilder> {
private:
	std::string_view name;


public:
	// The communication interface string. Optional.
	constexpr USBCdcAcmBuilder& Name(std::string_view text) {
		name = text;
		return *this;
	}


	constexpr std::size_t StringCount() const {
		return name.empty() ? 0 : 1;
	}


	constexpr std::string_view String(std::size_t index) const {
		static_cast<void>(index);
		return name;
	}


	constexpr uint8 FunctionClass() const {
		return static_cast<uint8>(USBClassCode::Cdc);
	}


	constexpr uint8 FunctionSubClass() const {
		return USBCdcAcmSubClass;
	}


	constexpr uint8 FunctionProtocol() const {
		return USBCdcNoProtocol;
	}


	constexpr bool HasWinUsb() const {
		return false;
	}


	constexpr std::string_view WinUsbGuid() const {
		return std::string_view();
	}


	constexpr void Emit(USBFunctionEmitter& emitter) const {
		emitter.Interface(0, static_cast<uint8>(USBClassCode::Cdc), USBCdcAcmSubClass, USBCdcNoProtocol, name.empty() ? USBNoString : 0);
		std::array<uint8, 5> header = MakeUSBCdcHeaderDescriptor();
		emitter.Raw(header);
		std::array<uint8, 5> callManagement = MakeUSBCdcCallManagementDescriptor(emitter.InterfaceNumber(1));
		emitter.Raw(callManagement);
		std::array<uint8, 4> acm = MakeUSBCdcAcmDescriptor();
		emitter.Raw(acm);
		std::array<uint8, 5> unionDescriptor = MakeUSBCdcUnionDescriptor(emitter.InterfaceNumber(0), emitter.InterfaceNumber(1));
		emitter.Raw(unionDescriptor);
		emitter.Endpoint(0);

		emitter.Interface(1, USBCdcDataClassCode, USBCdcNoSubClass, USBCdcNoProtocol, USBNoString);
		emitter.Endpoint(1);
		emitter.Endpoint(2);
	}
};
