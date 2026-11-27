#pragma once
#include <array>
#include <cstddef>
#include <string_view>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorString.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <System/System.h>
#include <Utilities/CompileTime/TextLiteral.h>


// Optional component: the URL descriptor that WebUSB GET_URL returns. A landing page URL that starts with https:// or http://
// is sent as a scheme code and the rest of the text, so the browser prepends the prefix itself. Any other text is sent whole
// with the no-prefix scheme code.


// bDescriptorType of a WebUSB URL descriptor. Not a standard USB descriptor type; the WebUSB specification assigns it.
constexpr uint8 USBWebUsbUrlDescriptorType = 3;

// bScheme values of the URL descriptor.
constexpr uint8 USBWebUsbSchemeHttp = 0;
constexpr uint8 USBWebUsbSchemeHttps = 1;
constexpr uint8 USBWebUsbSchemeNone = 255;

// bLength is one byte and the descriptor has a 3-byte header, so the text after the scheme is at most 252 bytes.
constexpr std::size_t USBWebUsbMaxUrlBytes = 252;


// The scheme code of a URL: https:// and http:// have their own codes, and any other text has none. Only the lowercase prefixes
// are recognised. An upper-case prefix gets the no-prefix code and is sent whole, which is valid because URL schemes are
// case-insensitive.
constexpr uint8 USBWebUsbSchemeOf(std::string_view url) {
	if (url.substr(0, 8) == "https://") {
		return USBWebUsbSchemeHttps;
	}

	if (url.substr(0, 7) == "http://") {
		return USBWebUsbSchemeHttp;
	}

	return USBWebUsbSchemeNone;
}


// The URL bytes the descriptor carries: the text without the scheme prefix that its code stands for.
constexpr std::string_view USBWebUsbRestOf(std::string_view url) {
	uint8 scheme = USBWebUsbSchemeOf(url);
	if (scheme == USBWebUsbSchemeHttps) {
		return url.substr(8);
	}

	if (scheme == USBWebUsbSchemeHttp) {
		return url.substr(7);
	}

	return url;
}


// Appends the URL descriptor for url: bLength, bDescriptorType 3, bScheme, then the UTF-8 text without a terminator.
constexpr void USBAppendWebUsbUrlDescriptor(USBBuildBuffer& buffer, std::string_view url) {
	Require<"USB: a WebUSB URL is valid UTF-8">(USBCountUtf16Units(url.data(), url.size()).valid);
	std::string_view rest = USBWebUsbRestOf(url);
	Require<"USB: a WebUSB URL has text after its scheme">(!rest.empty());
	Require<"USB: a WebUSB URL is at most 252 bytes after its scheme">(rest.size() <= USBWebUsbMaxUrlBytes);
	buffer.Put(static_cast<uint8>(3 + rest.size()));
	buffer.Put(USBWebUsbUrlDescriptorType);
	buffer.Put(USBWebUsbSchemeOf(url));
	for (char character : rest) {
		buffer.Put(static_cast<uint8>(character));
	}
}


// Size of the URL descriptor of a literal, for the array type of MakeWebUsbUrlDescriptor. The length check fails the build here
// when the text is too long, so the error names the limit.
template<TextLiteral Url>
constexpr std::size_t USBWebUsbUrlDescriptorSize() {
	constexpr std::string_view rest = USBWebUsbRestOf(std::string_view(Url.value, Url.GetLength()));
	static_assert(rest.size() <= USBWebUsbMaxUrlBytes, "a WebUSB URL is at most 252 bytes after its scheme");
	return 3 + rest.size();
}


// The URL descriptor of a literal, as a fixed array of exactly its size.
template<TextLiteral Url>
constexpr std::array<uint8, USBWebUsbUrlDescriptorSize<Url>()> MakeWebUsbUrlDescriptor() {
	USBBuildBuffer buffer;
	USBAppendWebUsbUrlDescriptor(buffer, std::string_view(Url.value, Url.GetLength()));
	return USBBuildExact<USBWebUsbUrlDescriptorSize<Url>()>(buffer);
}
