#pragma once
#include <cstddef>
#include <string_view>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>


// A class builder for the emitter's alternate-setting rules. Emit() writes interface 0 as alternate 0 when Default is true, then one
// alternate setting for each value in Alternates, in the order given. The DFU builder always writes its alternates in order, so the
// snippets use this builder to reach each rule of Alternate() on its own.
template<bool Default, uint8... Alternates>
struct AlternateFunction {
	constexpr std::size_t InterfaceCount() const {
		return 1;
	}


	constexpr std::size_t EndpointCount() const {
		return 0;
	}


	constexpr USBEndpointRequest Endpoint(std::size_t index) const {
		static_cast<void>(index);
		return USBEndpointRequest{};
	}


	constexpr std::size_t StringCount() const {
		return 0;
	}


	constexpr std::string_view String(std::size_t index) const {
		static_cast<void>(index);
		return std::string_view();
	}


	constexpr uint8 FunctionClass() const {
		return 0xFE;
	}


	constexpr uint8 FunctionSubClass() const {
		return 0x01;
	}


	constexpr uint8 FunctionProtocol() const {
		return 0x01;
	}


	constexpr bool HasWinUsb() const {
		return false;
	}


	constexpr std::string_view WinUsbGuid() const {
		return std::string_view();
	}


	constexpr void Emit(USBFunctionEmitter& emitter) const {
		if constexpr (Default) {
			emitter.Interface(0, 0xFE, 0x01, 0x01, USBNoString);
		}

		(emitter.Alternate(0, Alternates, 0xFE, 0x01, 0x01, USBNoString), ...);
	}
};
