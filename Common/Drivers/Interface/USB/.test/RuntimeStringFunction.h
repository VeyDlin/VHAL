#pragma once
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <array>
#include <cstddef>
#include <string_view>


// A class builder for the run-time string tests. It reserves Count run-time strings, and writes each one into its own class-specific
// descriptor {5, 0x24, 0x7F, index, position}. The index byte is at offset 3, as iMACAddress is in the Ethernet functional descriptor.
// The position byte at offset 4 counts the function's own run-time strings from 0, so a test can tell the descriptors apart.
//
// Number sets the bulk endpoints to 0x80 | Number and Number. The other parameters break the rules on purpose, for the error snippets:
//   Write      false writes no index at all, so the reserved strings are never referenced
//   Reference  the position the descriptors pass, so Reference + Count past Count is outside the function
//   Byte       the value the byte at offset 3 is given as; anything but 0 is refused
template<std::size_t Count, uint8 Number = 1, bool Write = true, std::size_t Reference = 0, uint8 Byte = 0>
class RuntimeStringFunction {
public:
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
		request.address = index == 0 ? static_cast<uint8>(0x80 | Number) : Number;
		request.maxPacketSize = 64;
		return request;
	}


	constexpr std::size_t StringCount() const {
		return 0;
	}


	constexpr std::string_view String(std::size_t index) const {
		static_cast<void>(index);
		return std::string_view();
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
		return false;
	}


	constexpr std::string_view WinUsbGuid() const {
		return std::string_view();
	}


	constexpr std::size_t RuntimeStringCount() const {
		return Count;
	}


	constexpr void Emit(USBFunctionEmitter& emitter) const {
		emitter.Interface(0, static_cast<uint8>(USBClassCode::Vendor), 0x00, 0x00, USBNoString);
		for (std::size_t index = 0; index < Count; index++) {
			if constexpr (Write) {
				std::array<uint8, 5> bytes = { 0x05, 0x24, 0x7F, Byte, static_cast<uint8>(index) };
				emitter.RawWithRuntimeString(bytes, 3, Reference + index);
			}
		}

		emitter.Endpoint(0);
		emitter.Endpoint(1);
	}
};
