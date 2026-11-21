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


// Class builder of a CDC-NCM function (USBConfigurationBuilder::Function()): an Ethernet link over USB (network control model). It has
// two interfaces: the communication interface (class 0x02, subclass 0x0D, protocol 0x00) and the data interface (class 0x0A, protocol
// 0x01, NTB framing). An interface association covers both. The Windows 10 2004 and later UsbNcm driver, the Linux cdc_ncm driver and
// the macOS driver bind their in-box drivers to the class codes: no INF and no kernel extension.
//
// Layout of the interfaces, as the configuration writes them:
//   communication interface 0: Header (bcdCDC 0x0110), Union (control 0, subordinate 1), Ethernet Networking (iMACAddress is a
//                              run-time string, see below), NCM (bmNetworkCapabilities: packet filter and NTB input size), then the
//                              notification endpoint;
//   data interface 1:          alternate 0 with no endpoints (no traffic), alternate 1 with the bulk IN and bulk OUT endpoints.
//
// The host selects alternate 1 to start the data path. The data interface is the only one with alternates, so the configuration
// lists the two settings in order (Interface() then Alternate()).
//
// The endpoints, their addresses and their order are the ones USBCdcEndpointBuilder describes, and NcmFunction reads them in that order.
// The notification packet is 16 bytes, as the CONNECTION_SPEED_CHANGE notification needs; its interval defaults to 16 ms.
//
// The MAC address. The Ethernet descriptor names the host's MAC address in iMACAddress: 12 upper-case hex digits. That is the address
// the host gives its own network interface, so the device's IP stack must use a different one. The function reserves one run-time
// string for it. The application builds the descriptor with MakeUSBHexStringDescriptor() and gives it to USBDevice::SetRuntimeString()
// with the index NcmFunction::MacStringIndex() returns.
//
// Name() is the communication interface's string. The data interface has none.
class USBCdcNcmBuilder : public USBCdcEndpointBuilder<USBCdcNcmBuilder> {
public:
	// bmNetworkCapabilities as NcmFunction implements them: SET_ETHERNET_PACKET_FILTER (bit 0) and the NTB input size, in its 8-byte
	// form with the datagram count (bit 5). Nothing else is advertised.
	static constexpr uint8 Capabilities = USBCdcNcmCapPacketFilter | USBCdcNcmCapNtbInputSize;


private:
	std::string_view name;


public:
	// The communication interface string. Optional.
	constexpr USBCdcNcmBuilder& Name(std::string_view text) {
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


	// One run-time string: the host's MAC address, named by iMACAddress of the Ethernet descriptor.
	constexpr std::size_t RuntimeStringCount() const {
		return 1;
	}


	constexpr uint8 FunctionClass() const {
		return static_cast<uint8>(USBClassCode::Cdc);
	}


	constexpr uint8 FunctionSubClass() const {
		return USBCdcNcmSubClass;
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
		emitter.Interface(0, static_cast<uint8>(USBClassCode::Cdc), USBCdcNcmSubClass, USBCdcNoProtocol, name.empty() ? USBNoString : 0);
		std::array<uint8, 5> header = MakeUSBCdcHeaderDescriptor();
		emitter.Raw(header);
		std::array<uint8, 5> unionDescriptor = MakeUSBCdcUnionDescriptor(emitter.InterfaceNumber(0), emitter.InterfaceNumber(1));
		emitter.Raw(unionDescriptor);
		std::array<uint8, USBCdcEthernetDescriptorLength> ethernet = MakeUSBCdcEthernetDescriptor();
		emitter.RawWithRuntimeString(ethernet, 3, 0);
		std::array<uint8, USBCdcNcmDescriptorLength> ncm = MakeUSBCdcNcmDescriptor(Capabilities);
		emitter.Raw(ncm);
		emitter.Endpoint(0);

		emitter.Interface(1, USBCdcDataClassCode, USBCdcNoSubClass, USBCdcNtbProtocol, USBNoString);
		emitter.Alternate(1, 1, USBCdcDataClassCode, USBCdcNoSubClass, USBCdcNtbProtocol, USBNoString);
		emitter.Endpoint(1);
		emitter.Endpoint(2);
	}
};
