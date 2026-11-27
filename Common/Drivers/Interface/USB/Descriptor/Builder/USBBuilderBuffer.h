#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorString.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <System/System.h>
#include <Utilities/CompileTime/Require.h>


// Shared pieces of the fluent builder. A chain is evaluated in a constant expression: the application writes it inside a
// captureless lambda passed to USBDescriptors, and every rule violation is a Require check (Utilities/CompileTime/Require.h).
// A run-time call of a check links to nothing, which is the intended failure for a builder that is not evaluated at compile
// time.


// Largest byte count one builder stage produces. Every stage writes into a buffer of this size, and the result is copied into
// an array of exact size, so the capacity bounds the chain and never reaches flash.
constexpr std::size_t USBBuildCapacity = 1024;


// Fixed-capacity byte sequence the builders append to. size is the number of bytes written; the rest is zero and unused.
struct USBBuildBuffer {
	std::array<uint8, USBBuildCapacity> bytes{};
	std::size_t size = 0;


	constexpr void Put(uint8 value) {
		Require<"USB: descriptor is larger than the builder capacity">(size < USBBuildCapacity);
		bytes[size] = value;
		size++;
	}


	constexpr void Put16(uint16 value) {
		Put(static_cast<uint8>(value & 0xFF));
		Put(static_cast<uint8>(value >> 8));
	}


	constexpr void Append(std::span<const uint8> piece) {
		for (uint8 value : piece) {
			Put(value);
		}
	}
};


// Copies the first Size bytes of a buffer into an array of exact size. The caller computed Size from the same chain, so a
// mismatch is a builder bug and is reported.
template<std::size_t Size>
constexpr std::array<uint8, Size> USBBuildExact(const USBBuildBuffer& buffer) {
	Require<"USB: builder output does not match its size">(buffer.size == Size);
	std::array<uint8, Size> exact{};
	for (std::size_t index = 0; index < Size; index++) {
		exact[index] = buffer.bytes[index];
	}

	return exact;
}


// Appends a string descriptor for a run-time text (a string view of a literal). The text is checked the same way as a
// TextLiteral template argument: valid UTF-8, no surrogates, at most 126 UTF-16 code units.
constexpr void USBBuildStringDescriptor(std::string_view text, USBBuildBuffer& buffer) {
	USBUtf16Count count = USBCountUtf16Units(text.data(), text.size());
	Require<"USB: string is not valid UTF-8">(count.valid);
	Require<"USB: a string descriptor holds at most 126 UTF-16 code units">(count.units <= 126);
	buffer.Put(static_cast<uint8>(2 + 2 * count.units));
	buffer.Put(static_cast<uint8>(USBDescriptorType::String));
	buffer.size = USBEncodeUtf16(text.data(), text.size(), buffer.bytes, buffer.size);
}
