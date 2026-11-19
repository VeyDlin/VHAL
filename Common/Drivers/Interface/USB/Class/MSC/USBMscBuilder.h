#pragma once
#include <cstddef>
#include <string_view>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Class/MSC/MscProtocol.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// Class builder of the mass storage function: one interface of class 0x08, subclass 0x06 (SCSI transparent) and protocol 0x50
// (Bulk-Only Transport), with a bulk IN and a bulk OUT endpoint of 64-byte packets and interval 0. The IN endpoint comes first in
// the layout, then the OUT endpoint. An address left out is assigned by the configuration builder. The optional name is the
// interface string. The host's in-box driver binds the interface, so the function needs no WinUSB.
class USBMscBuilder {
private:
	uint8 bulkInAddress = 0;
	uint8 bulkOutAddress = 0;
	std::string_view name;


public:
	constexpr USBMscBuilder& Name(std::string_view text) {
		name = text;
		return *this;
	}


	// An explicit address is an endpoint number 1 to 15 with the matching direction. Address 0 means "assigned by the builder".
	constexpr USBMscBuilder& BulkIn(uint8 address) {
		Require<USBText{"BulkIn needs an IN address (bit 7 set)"}>(USBEndpoint::IsIn(address));
		Require<USBText{"endpoint 0 belongs to the control pipe"}>(USBEndpoint::Number(address) != 0);
		bulkInAddress = address;
		return *this;
	}


	constexpr USBMscBuilder& BulkOut(uint8 address) {
		Require<USBText{"BulkOut needs an OUT address (bit 7 clear)"}>(!USBEndpoint::IsIn(address));
		Require<USBText{"endpoint 0 belongs to the control pipe"}>(USBEndpoint::Number(address) != 0);
		bulkOutAddress = address;
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
		request.maxPacketSize = MscPacketSize;
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
		return MscInterfaceClass;
	}


	constexpr uint8 FunctionSubClass() const {
		return MscSubClassScsi;
	}


	constexpr uint8 FunctionProtocol() const {
		return MscProtocolBulkOnly;
	}


	// The in-box mass storage driver of each host binds the interface, so no WinUSB descriptor is written.
	constexpr bool HasWinUsb() const {
		return false;
	}


	constexpr std::string_view WinUsbGuid() const {
		return std::string_view();
	}


	constexpr void Emit(USBFunctionEmitter& emitter) const {
		emitter.Interface(0, MscInterfaceClass, MscSubClassScsi, MscProtocolBulkOnly, name.empty() ? USBNoString : 0);
		emitter.Endpoint(0);
		emitter.Endpoint(1);
	}
};
