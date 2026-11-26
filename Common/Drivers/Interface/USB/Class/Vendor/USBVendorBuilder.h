#pragma once
#include <cstddef>
#include <string_view>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// Class builder of the vendor bulk function (VendorBulkFunctionBase): one interface of class 0xFF with a bulk IN and a bulk OUT
// endpoint, 64-byte packets, interval 0. The IN endpoint is first in the layout, the OUT endpoint second. An address left out is
// assigned by the configuration builder. The optional name is the interface string, and WinUsb() binds WinUSB to the interface
// through the MS OS 2.0 descriptors when the device has them.
class USBVendorBuilder {
private:
	uint8 bulkInAddress = 0;
	uint8 bulkOutAddress = 0;
	std::string_view name;
	bool winUsb = false;
	std::string_view guid;


public:
	constexpr USBVendorBuilder& Name(std::string_view text) {
		name = text;
		return *this;
	}


	// An explicit address is an endpoint number 1 to 15 with the matching direction. Address 0 means "assigned by the builder",
	// so an explicit OUT 0x00 would be taken for no address at all and is rejected here.
	constexpr USBVendorBuilder& BulkIn(uint8 address) {
		Require<"USB:BulkIn needs an IN address (bit 7 set)">(USBEndpoint::IsIn(address));
		Require<"USB:endpoint 0 belongs to the control pipe">(USBEndpoint::Number(address) != 0);
		bulkInAddress = address;
		return *this;
	}


	constexpr USBVendorBuilder& BulkOut(uint8 address) {
		Require<"USB:BulkOut needs an OUT address (bit 7 clear)">(!USBEndpoint::IsIn(address));
		Require<"USB:endpoint 0 belongs to the control pipe">(USBEndpoint::Number(address) != 0);
		bulkOutAddress = address;
		return *this;
	}


	// Binds WinUSB to the interface. Needs MsOs20VendorCode() on the device.
	constexpr USBVendorBuilder& WinUsb() {
		winUsb = true;
		return *this;
	}


	// Binds WinUSB and names the interface GUID that applications open the device with, for example "{...}" in registry form.
	constexpr USBVendorBuilder& WinUsb(std::string_view interfaceGuid) {
		winUsb = true;
		guid = interfaceGuid;
		return *this;
	}


	constexpr std::size_t InterfaceCount() const {
		return 1;
	}


	constexpr std::size_t EndpointCount() const {
		return 2;
	}


	constexpr USBEndpointRequest Endpoint(std::size_t index) const {
		USBEndpointRequest request;
		request.type = USBEndpointType::Bulk;
		request.direction = index == 0 ? USBDirection::In : USBDirection::Out;
		request.address = index == 0 ? bulkInAddress : bulkOutAddress;
		request.maxPacketSize = 64;
		request.interval = 0;
		return request;
	}


	constexpr std::size_t StringCount() const {
		return name.empty() ? 0 : 1;
	}


	constexpr std::string_view String(std::size_t index) const {
		static_cast<void>(index);
		return name;
	}


	constexpr uint8 FunctionClass() const {
		return static_cast<uint8>(USBClassCode::Vendor);
	}


	constexpr uint8 FunctionSubClass() const {
		return 0x00;
	}


	constexpr uint8 FunctionProtocol() const {
		return 0x00;
	}


	constexpr bool HasWinUsb() const {
		return winUsb;
	}


	constexpr std::string_view WinUsbGuid() const {
		return guid;
	}


	constexpr void Emit(USBFunctionEmitter& emitter) const {
		emitter.Interface(0, static_cast<uint8>(USBClassCode::Vendor), 0x00, 0x00, name.empty() ? USBNoString : 0);
		emitter.Endpoint(0);
		emitter.Endpoint(1);
	}
};
