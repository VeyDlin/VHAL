#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// The HID descriptor of HID 1.11 section 6.2.1: bcdHID 0x0111, no country code, one report descriptor of the given length.
constexpr std::array<uint8, 9> MakeUSBHidDescriptor(uint16 reportDescriptorLength) {
	std::array<uint8, 9> bytes{};
	bytes[0] = 9;
	bytes[1] = 0x21;
	USBDescriptorPut16(bytes, 2, 0x0111);
	bytes[4] = 0;
	bytes[5] = 1;
	bytes[6] = 0x22;
	USBDescriptorPut16(bytes, 7, reportDescriptorLength);
	return bytes;
}


// Class builder of one HID interface (USBConfigurationBuilder::Function()). The interface has an interrupt IN endpoint, and an
// interrupt OUT endpoint when OutEndpoint() or Out() is given. The HID descriptor sits between the interface and its endpoints.
//
// BootKeyboard() and BootMouse() make a boot device: subclass 1 and protocol 1 (keyboard) or 2 (mouse). The boot report is the
// first 8 (keyboard) or 3 (mouse) bytes of the input report, so the report must have no report IDs and be long enough. Any other
// interface has subclass 0 and protocol 0.
//
// The report descriptor is given with Report(). Its bytes are not part of the configuration: the HID descriptor carries only their
// length. The HID function takes the same USBHidReportInfo at run time, so the bytes the host reads and the bytes the function
// answers with come from one object.
//
// The IN packet size is the largest input report including its ID byte, and the OUT packet size the largest output report, unless
// PacketSize() is given. A packet size is at most 64 and holds the largest report of its direction.
class USBHidBuilder {
private:
	bool bootKeyboard = false;
	bool bootMouse = false;
	bool hasReport = false;
	USBHidReportInfo report;
	uint8 inAddress = 0;
	bool hasOut = false;
	uint8 outAddress = 0;
	uint8 intervalMs = 1;
	uint16 packetSize = 0;
	std::string_view name;


public:
	// Subclass 1, protocol 1, and the boot keyboard rules.
	constexpr USBHidBuilder& BootKeyboard() {
		bootKeyboard = true;
		bootMouse = false;
		return *this;
	}


	// Subclass 1, protocol 2, and the boot mouse rules.
	constexpr USBHidBuilder& BootMouse() {
		bootMouse = true;
		bootKeyboard = false;
		return *this;
	}


	// The report descriptor of the interface, from USBHidReportDescriptor<...>::Info. Required.
	constexpr USBHidBuilder& Report(const USBHidReportInfo& info) {
		report = info;
		hasReport = true;
		return *this;
	}


	// Address of the interrupt IN endpoint. Without it the configuration builder assigns the number.
	constexpr USBHidBuilder& In(uint8 address) {
		Require<"USB: In() needs an IN address (bit 7 set)">(USBEndpoint::IsIn(address));
		Require<"USB: endpoint 0 belongs to the control pipe">(USBEndpoint::Number(address) != 0);
		inAddress = address;
		return *this;
	}


	// An interrupt OUT endpoint, with an address the configuration builder assigns.
	constexpr USBHidBuilder& OutEndpoint() {
		hasOut = true;
		return *this;
	}


	// An interrupt OUT endpoint with the given address.
	constexpr USBHidBuilder& Out(uint8 address) {
		Require<"USB: Out() needs an OUT address (bit 7 clear)">(!USBEndpoint::IsIn(address));
		Require<"USB: endpoint 0 belongs to the control pipe">(USBEndpoint::Number(address) != 0);
		outAddress = address;
		hasOut = true;
		return *this;
	}


	// Polling interval of both interrupt endpoints, 1 to 255 ms. Default 1.
	constexpr USBHidBuilder& IntervalMs(uint8 milliseconds) {
		Require<"USB: an interrupt interval is 1 to 255 ms">(milliseconds >= 1);
		intervalMs = milliseconds;
		return *this;
	}


	// Packet size of both interrupt endpoints, 1 to 64. Default: the largest report of each direction.
	constexpr USBHidBuilder& PacketSize(uint16 bytes) {
		Require<"USB: a packet size is 1 to 64 bytes">(bytes >= 1 && bytes <= 64);
		packetSize = bytes;
		return *this;
	}


	// The interface string.
	constexpr USBHidBuilder& Name(std::string_view text) {
		name = text;
		return *this;
	}


	constexpr std::size_t InterfaceCount() const {
		Require<"USB: a HID interface needs a report descriptor: call Report()">(hasReport);
		Require<"USB: a HID interface sends at least one input report">(report.MaxLength(USBHidReportType::Input) > 0);
		if (bootKeyboard) {
			Require<"USB: a boot keyboard sends an 8-byte input report without a report ID">(report.InputLength(0) >= 8);
		}

		if (bootMouse) {
			Require<"USB: a boot mouse sends a 3-byte input report without a report ID">(report.InputLength(0) >= 3);
		}

		return 1;
	}


	constexpr std::size_t EndpointCount() const {
		return hasOut ? 2 : 1;
	}


	// The IN endpoint first, then the OUT endpoint. The function's layout lists them in the same order.
	constexpr USBEndpointRequest Endpoint(std::size_t index) const {
		USBEndpointRequest request;
		request.type = USBEndpointType::Interrupt;
		request.direction = index == 0 ? USBDirection::In : USBDirection::Out;
		request.address = index == 0 ? inAddress : outAddress;
		request.maxPacketSize = index == 0 ? InPacketSize() : OutPacketSize();
		request.interval = intervalMs;
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
		return static_cast<uint8>(USBClassCode::Hid);
	}


	constexpr uint8 FunctionSubClass() const {
		return bootKeyboard || bootMouse ? 0x01 : 0x00;
	}


	constexpr uint8 FunctionProtocol() const {
		if (bootKeyboard) {
			return 0x01;
		}

		return bootMouse ? 0x02 : 0x00;
	}


	constexpr bool HasWinUsb() const {
		return false;
	}


	constexpr std::string_view WinUsbGuid() const {
		return std::string_view();
	}


	constexpr void Emit(USBFunctionEmitter& emitter) const {
		emitter.Interface(0, static_cast<uint8>(USBClassCode::Hid), FunctionSubClass(), FunctionProtocol(), name.empty() ? USBNoString : 0);
		std::array<uint8, 9> hid = MakeUSBHidDescriptor(static_cast<uint16>(report.descriptor.size()));
		emitter.Raw(hid);
		emitter.Endpoint(0);
		if (hasOut) {
			emitter.Endpoint(1);
		}
	}


private:
	constexpr uint16 InPacketSize() const {
		std::size_t largest = report.MaxLength(USBHidReportType::Input);
		uint16 size = packetSize != 0 ? packetSize : static_cast<uint16>(largest);
		Require<"USB: the packet size holds the largest input report">(size >= largest);
		Require<"USB: a packet size is 1 to 64 bytes">(size >= 1 && size <= 64);
		return size;
	}


	constexpr uint16 OutPacketSize() const {
		std::size_t largest = report.MaxLength(USBHidReportType::Output);
		Require<"USB: OutEndpoint() needs an output report">(largest >= 1);
		uint16 size = packetSize != 0 ? packetSize : static_cast<uint16>(largest);
		Require<"USB: the packet size holds the largest output report">(size >= largest);
		Require<"USB: a packet size is 1 to 64 bytes">(size >= 1 && size <= 64);
		return size;
	}
};
