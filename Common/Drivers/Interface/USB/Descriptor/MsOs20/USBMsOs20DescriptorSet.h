#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorString.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Utilities/TextLiteral.h>


// Optional component: the Microsoft OS 2.0 descriptor set, served by a vendor request with wIndex 7. Every length is computed
// by the builders, and a child whose length field disagrees with its size is rejected when it is appended.


// wIndex of the request that fetches the descriptor set. Fixed by the specification.
constexpr uint16 USBMsOs20DescriptorIndex = 7;


enum class USBMsOs20DescriptorType : uint16 {
	SetHeader = 0x0000,
	ConfigurationSubsetHeader = 0x0001,
	FunctionSubsetHeader = 0x0002,
	CompatibleId = 0x0003,
	RegistryProperty = 0x0004
};


// wPropertyDataType of a registry property: the Windows REG_* value.
enum class USBMsOs20PropertyType : uint16 {
	String = 1,
	ExpandString = 2,
	Binary = 3,
	DwordLittleEndian = 4,
	MultiString = 7
};


// Appends one element. Its length field must equal its size: wLength at offset 0 for a feature, wSubsetLength or
// wTotalLength at offset 6 for a subset.
template<std::size_t Size>
constexpr std::size_t USBMsOs20AppendElement(
	std::array<uint8, Size>& bytes,
	std::size_t offset,
	std::span<const uint8> element,
	std::size_t lengthOffset)
{
	USBDescriptorRequire(USBDescriptorRead16(element, lengthOffset) == element.size(), "an MS OS 2.0 element's length field must equal its size");
	return USBDescriptorAppend(bytes, offset, element);
}


// Appends one subset. A descriptor set holds configuration and function subsets; a configuration subset holds function subsets
// only. allowConfiguration tells the two levels apart, so a feature or a subset of the wrong level is named by the rule.
template<std::size_t Size>
constexpr std::size_t USBMsOs20AppendSubset(
	std::array<uint8, Size>& bytes,
	std::size_t offset,
	std::span<const uint8> subset,
	bool allowConfiguration)
{
	uint16 type = USBDescriptorRead16(subset, 2);
	bool configurationSubset = type == static_cast<uint16>(USBMsOs20DescriptorType::ConfigurationSubsetHeader);
	bool functionSubset = type == static_cast<uint16>(USBMsOs20DescriptorType::FunctionSubsetHeader);
	USBDescriptorRequire(
		functionSubset || (allowConfiguration && configurationSubset),
		"a set holds configuration and function subsets, a configuration subset holds function subsets only");
	return USBMsOs20AppendElement(bytes, offset, subset, 6);
}


// Copies ASCII text into the zero-padded field at offset. Compatible IDs are ASCII.
template<std::size_t Size>
constexpr void USBMsOs20CopyAscii(std::array<uint8, Size>& bytes, std::size_t offset, const char* text, std::size_t length) {
	for (std::size_t index = 0; index < length; index++) {
		uint8 character = static_cast<uint8>(text[index]);
		USBDescriptorRequire(character < 0x80, "compatible IDs are ASCII");
		USBDescriptorPut8(bytes, offset + index, character);
	}
}


// Compatible ID feature (20 bytes): an 8-character ID and an 8-character sub-ID, each zero-padded. "WINUSB" selects WinUSB.
template<std::size_t IdSize, std::size_t SubIdSize>
constexpr std::array<uint8, 20> MakeMsOs20CompatibleId(
	const char (&compatibleId)[IdSize],
	const char (&subCompatibleId)[SubIdSize])
{
	USBDescriptorRequire(IdSize - 1 <= 8, "a compatible ID is at most 8 characters");
	USBDescriptorRequire(SubIdSize - 1 <= 8, "a sub-compatible ID is at most 8 characters");

	std::array<uint8, 20> bytes{};
	USBDescriptorPut16(bytes, 0, 20);
	USBDescriptorPut16(bytes, 2, static_cast<uint16>(USBMsOs20DescriptorType::CompatibleId));
	USBMsOs20CopyAscii(bytes, 4, compatibleId, IdSize - 1);
	USBMsOs20CopyAscii(bytes, 12, subCompatibleId, SubIdSize - 1);
	return bytes;
}


// Registry property feature. The name is UTF-16LE with its terminator (taken from the text); the data is the value's bytes,
// for example from MakeMsOs20MultiStringData() or MakeUSBUtf16LENullTerminated().
template<TextLiteral Name, std::size_t DataSize>
constexpr std::array<uint8, 10 + 2 * (USBUtf16UnitCount<Name>() + 1) + DataSize> MakeMsOs20RegistryProperty(
	USBMsOs20PropertyType propertyType,
	const std::array<uint8, DataSize>& data)
{
	constexpr std::size_t nameBytes = 2 * (USBUtf16UnitCount<Name>() + 1);
	constexpr std::size_t total = 10 + nameBytes + DataSize;
	USBDescriptorRequire(total <= 0xFFFF, "a registry property is at most 65535 bytes");

	std::array<uint8, total> bytes{};
	USBDescriptorPut16(bytes, 0, static_cast<uint16>(total));
	USBDescriptorPut16(bytes, 2, static_cast<uint16>(USBMsOs20DescriptorType::RegistryProperty));
	USBDescriptorPut16(bytes, 4, static_cast<uint16>(propertyType));
	USBDescriptorPut16(bytes, 6, static_cast<uint16>(nameBytes));
	std::array<uint8, nameBytes> name = MakeUSBUtf16LENullTerminated<Name>();
	std::size_t offset = USBDescriptorAppend(bytes, 8, name);
	USBDescriptorPut16(bytes, offset, static_cast<uint16>(DataSize));
	USBDescriptorAppend(bytes, offset + 2, data);
	return bytes;
}


// REG_MULTI_SZ data: each string as UTF-16LE with its terminator, then one more terminator. Each string comes from
// MakeUSBUtf16LENullTerminated<...>().
template<typename... Strings>
constexpr std::array<uint8, (std::tuple_size_v<Strings> + ... + 2)> MakeMsOs20MultiStringData(const Strings&... strings) {
	constexpr std::size_t total = (std::tuple_size_v<Strings> + ... + 2);
	std::array<uint8, total> bytes{};
	std::size_t offset = 0;
	(static_cast<void>(offset = USBDescriptorAppend(bytes, offset, strings)), ...);
	return bytes;
}


// Function subset: the features of the function whose first interface is firstInterface. Needs at least one feature.
template<typename... Features>
constexpr std::array<uint8, 8 + (std::tuple_size_v<Features> + ... + 0)> MakeMsOs20FunctionSubset(
	uint8 firstInterface,
	const Features&... features)
{
	constexpr std::size_t total = 8 + (std::tuple_size_v<Features> + ... + 0);
	USBDescriptorRequire(sizeof...(Features) >= 1, "a function subset holds at least one feature");
	USBDescriptorRequire(total <= 0xFFFF, "wSubsetLength is 16 bits");

	std::array<uint8, total> bytes{};
	USBDescriptorPut16(bytes, 0, 8);
	USBDescriptorPut16(bytes, 2, static_cast<uint16>(USBMsOs20DescriptorType::FunctionSubsetHeader));
	bytes[4] = firstInterface;
	USBDescriptorPut16(bytes, 6, static_cast<uint16>(total));
	std::size_t offset = 8;
	(static_cast<void>(offset = USBMsOs20AppendElement(bytes, offset, features, 0)), ...);
	return bytes;
}


// Configuration subset: the function subsets of one configuration. Optional in a set; use it when the device has several
// configurations or the functions must be grouped under one.
template<typename... Functions>
constexpr std::array<uint8, 8 + (std::tuple_size_v<Functions> + ... + 0)> MakeMsOs20ConfigurationSubset(
	uint8 configurationValue,
	const Functions&... functions)
{
	constexpr std::size_t total = 8 + (std::tuple_size_v<Functions> + ... + 0);
	USBDescriptorRequire(sizeof...(Functions) >= 1, "a configuration subset holds at least one function subset");
	USBDescriptorRequire(configurationValue >= 1, "a configuration subset names a configuration value");
	USBDescriptorRequire(total <= 0xFFFF, "wTotalLength is 16 bits");

	std::array<uint8, total> bytes{};
	USBDescriptorPut16(bytes, 0, 8);
	USBDescriptorPut16(bytes, 2, static_cast<uint16>(USBMsOs20DescriptorType::ConfigurationSubsetHeader));
	bytes[4] = configurationValue;
	USBDescriptorPut16(bytes, 6, static_cast<uint16>(total));
	std::size_t offset = 8;
	(static_cast<void>(offset = USBMsOs20AppendSubset(bytes, offset, functions, false)), ...);
	USBDescriptorRequire(offset == total, "the configuration subset's children fill its length");
	return bytes;
}


// The descriptor set: a 10-byte header with dwWindowsVersion (for example 0x06030000 for Windows 8.1), then the
// configuration and function subsets in the order given.
template<typename... Subsets>
constexpr std::array<uint8, 10 + (std::tuple_size_v<Subsets> + ... + 0)> MakeMsOs20DescriptorSet(
	uint32 windowsVersion,
	const Subsets&... subsets)
{
	constexpr std::size_t total = 10 + (std::tuple_size_v<Subsets> + ... + 0);
	USBDescriptorRequire(sizeof...(Subsets) >= 1, "a descriptor set holds at least one subset");
	USBDescriptorRequire(total <= 0xFFFF, "wTotalLength is 16 bits");

	std::array<uint8, total> bytes{};
	USBDescriptorPut16(bytes, 0, 10);
	USBDescriptorPut16(bytes, 2, static_cast<uint16>(USBMsOs20DescriptorType::SetHeader));
	USBDescriptorPut32(bytes, 4, windowsVersion);
	USBDescriptorPut16(bytes, 8, static_cast<uint16>(total));
	std::size_t offset = 10;
	(static_cast<void>(offset = USBMsOs20AppendSubset(bytes, offset, subsets, true)), ...);
	return bytes;
}


// Appends one feature of a device-level set. Only a compatible ID and a registry property may stand directly under the set header.
// Other features belong to a function subset.
template<std::size_t Size>
constexpr std::size_t USBMsOs20AppendFeature(std::array<uint8, Size>& bytes, std::size_t offset, std::span<const uint8> feature) {
	uint16 type = USBDescriptorRead16(feature, 2);
	bool compatibleId = type == static_cast<uint16>(USBMsOs20DescriptorType::CompatibleId);
	bool registryProperty = type == static_cast<uint16>(USBMsOs20DescriptorType::RegistryProperty);
	USBDescriptorRequire(compatibleId || registryProperty, "a device-level set holds compatible ID and registry property features only");
	return USBMsOs20AppendElement(bytes, offset, feature, 0);
}


// Device-level descriptor set: compatible ID and registry property features directly under the 10-byte set header, with no
// subsets. The Microsoft OS 2.0 Descriptors Specification allows this for a single-function device. A composite device needs
// MakeMsOs20DescriptorSet with one function subset per interface instead, because the features belong to a function there.
template<typename... Features>
constexpr std::array<uint8, 10 + (std::tuple_size_v<Features> + ... + 0)> MakeMsOs20DeviceLevelDescriptorSet(
	uint32 windowsVersion,
	const Features&... features)
{
	constexpr std::size_t total = 10 + (std::tuple_size_v<Features> + ... + 0);
	USBDescriptorRequire(sizeof...(Features) >= 1, "a device-level set holds at least one feature");
	USBDescriptorRequire(total <= 0xFFFF, "wTotalLength is 16 bits");

	std::array<uint8, total> bytes{};
	USBDescriptorPut16(bytes, 0, 10);
	USBDescriptorPut16(bytes, 2, static_cast<uint16>(USBMsOs20DescriptorType::SetHeader));
	USBDescriptorPut32(bytes, 4, windowsVersion);
	USBDescriptorPut16(bytes, 8, static_cast<uint16>(total));
	std::size_t offset = 10;
	(static_cast<void>(offset = USBMsOs20AppendFeature(bytes, offset, features)), ...);
	return bytes;
}


// The vendor request entry for the set, for USBDescriptorSet::vendorRequests. vendorCode is the bMS_VendorCode of the
// platform capability. The span must refer to storage that lives as long as the device (static constexpr).
constexpr USBVendorDescriptor MakeMsOs20VendorDescriptor(uint8 vendorCode, std::span<const uint8> descriptorSet) {
	return USBVendorDescriptor{ vendorCode, USBMsOs20DescriptorIndex, descriptorSet };
}
