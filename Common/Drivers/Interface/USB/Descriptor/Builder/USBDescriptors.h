#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDeviceBuilder.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20DescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/WebUsb/USBWebUsbPlatformCapability.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>


// The static storage of one device, built from a fluent chain. Usage:
//
//   using AppUsb = USBDescriptors<+[] {
//       return USBDeviceBuilder()
//           .VendorId(0x1209)
//           ...
//           .Configuration(USBConfigurationBuilder()
//               .MaxPowerMilliamps(100)
//               .Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x02)));
//   }>;
//
// The chain is a captureless lambda. The leading + converts it to the function pointer the template takes, so the parameter has
// an explicit type and no auto is needed. The chain runs at compile time. Each part is copied into an array of its exact size,
// a static constexpr member, so the spans of Set and Function() point into static storage and the device core can keep them for
// its lifetime. A rule violation in the chain is a compile error with its message.


// Copies the first Size bytes of a configuration body after its header check, sets bNumEndpoints of each interface and
// checks the composition (USBFinalizeConfiguration). The header's bNumInterfaces must equal the count it returns.
template<std::size_t Size>
constexpr std::array<uint8, Size> USBFinalizeExactConfiguration(const USBBuildBuffer& buffer) {
	std::array<uint8, Size> bytes = USBBuildExact<Size>(buffer);
	uint8 count = USBFinalizeConfiguration(bytes);
	Require<USBText{"bNumInterfaces must match the interfaces the configuration holds"}>(count == bytes[4]);
	return bytes;
}


// Splits the string bytes into one span per string descriptor, using the offsets the device builder recorded.
template<std::size_t Count>
constexpr std::array<std::span<const uint8>, Count> USBSliceStrings(
	std::span<const uint8> bytes,
	const std::array<std::size_t, USBMaxStrings + 2>& offsets)
{
	std::array<std::span<const uint8>, Count> table{};
	for (std::size_t index = 0; index < Count; index++) {
		table[index] = bytes.subspan(offsets[index], offsets[index + 1] - offsets[index]);
	}

	return table;
}


// The number of vendor request entries: one for the MS OS 2.0 set and one for WebUSB GET_URL, when the device has them.
constexpr std::size_t USBVendorRequestCount(const USBDescriptorParts& parts) {
	return (parts.msOs20 ? 1 : 0) + (parts.webUsb ? 1 : 0);
}


// The vendor request table, MS OS 2.0 first and GET_URL second. When both use one vendor code, the core tells the entries
// apart by wIndex (7 for the set, 2 for GET_URL).
template<std::size_t Count>
constexpr std::array<USBVendorDescriptor, Count> USBVendorTable(
	const USBDescriptorParts& parts,
	std::span<const uint8> msOs20Set,
	std::span<const uint8> webUsbUrl)
{
	std::array<USBVendorDescriptor, Count> table{};
	std::size_t index = 0;
	if (parts.msOs20) {
		table[index] = MakeMsOs20VendorDescriptor(parts.msOs20VendorCode, msOs20Set);
		index++;
	}

	if (parts.webUsb) {
		table[index] = MakeWebUsbVendorDescriptor(parts.webUsbVendorCode, USBWebUsbLandingPageIndex, webUsbUrl);
	}

	return table;
}


// The endpoint address table of all functions, from the flat array the device builder keeps.
template<std::size_t Count>
constexpr std::array<uint8, Count> USBCopyEndpointAddresses(const std::array<uint8, USBMaxEndpoints>& addresses) {
	std::array<uint8, Count> copy{};
	for (std::size_t index = 0; index < Count; index++) {
		copy[index] = addresses[index];
	}

	return copy;
}


// The first Count run-time string indexes, copied into an array of their exact size.
template<std::size_t Count>
constexpr std::array<uint8, Count> USBCopyRuntimeStringIndexes(const std::array<uint8, USBMaxRuntimeStrings>& indexes) {
	std::array<uint8, Count> copy{};
	for (std::size_t index = 0; index < Count; index++) {
		copy[index] = indexes[index];
	}

	return copy;
}


// Interface numbers 0 to Count-1, in order. The configuration numbers its interfaces this way.
template<std::size_t Count>
constexpr std::array<uint8, Count> USBSequentialInterfaces() {
	std::array<uint8, Count> numbers{};
	for (std::size_t index = 0; index < Count; index++) {
		numbers[index] = static_cast<uint8>(index);
	}

	return numbers;
}


// The parts of the chain Make builds. A namespace-scope variable template, so USBDescriptors can declare its public members
// first: a static member initializer needs its operands declared before it, and these parts are computed once per Make.
template<USBDeviceBuilder (*Make)()>
inline constexpr USBDescriptorParts USBBuiltParts = Make().Build();


template<USBDeviceBuilder (*Make)()>
class USBDescriptors {
public:
	static constexpr std::array<uint8, 18> Device = USBBuiltParts<Make>.device;
	static constexpr std::array<uint8, USBBuiltParts<Make>.configuration.size> Configuration =
		USBFinalizeExactConfiguration<USBBuiltParts<Make>.configuration.size>(USBBuiltParts<Make>.configuration);
	static constexpr std::array<uint8, USBBuiltParts<Make>.strings.size> StringBytes =
		USBBuildExact<USBBuiltParts<Make>.strings.size>(USBBuiltParts<Make>.strings);
	static constexpr std::array<std::span<const uint8>, USBBuiltParts<Make>.stringCount> StringTable =
		USBSliceStrings<USBBuiltParts<Make>.stringCount>(StringBytes, USBBuiltParts<Make>.stringOffsets);
	static constexpr std::array<uint8, USBBuiltParts<Make>.bos.size> Bos = USBBuildExact<USBBuiltParts<Make>.bos.size>(USBBuiltParts<Make>.bos);
	static constexpr std::array<uint8, USBBuiltParts<Make>.msOs20Set.size> MsOs20Set =
		USBBuildExact<USBBuiltParts<Make>.msOs20Set.size>(USBBuiltParts<Make>.msOs20Set);
	static constexpr std::array<uint8, USBBuiltParts<Make>.webUsbUrl.size> WebUsbUrl =
		USBBuildExact<USBBuiltParts<Make>.webUsbUrl.size>(USBBuiltParts<Make>.webUsbUrl);
	static constexpr std::array<USBVendorDescriptor, USBVendorRequestCount(USBBuiltParts<Make>)> VendorRequests =
		USBVendorTable<USBVendorRequestCount(USBBuiltParts<Make>)>(USBBuiltParts<Make>, MsOs20Set, WebUsbUrl);
	static constexpr std::array<uint8, USBBuiltParts<Make>.interfaceCount> InterfaceNumbers =
		USBSequentialInterfaces<USBBuiltParts<Make>.interfaceCount>();
	static constexpr std::array<uint8, USBBuiltParts<Make>.endpointAddressCount> EndpointAddresses =
		USBCopyEndpointAddresses<USBBuiltParts<Make>.endpointAddressCount>(USBBuiltParts<Make>.endpointAddresses);
	static constexpr std::array<uint8, USBBuiltParts<Make>.runtimeStringIndexCount> RuntimeStringIndexes =
		USBCopyRuntimeStringIndexes<USBBuiltParts<Make>.runtimeStringIndexCount>(USBBuiltParts<Make>.runtimeStringIndexes);
	static constexpr USBDescriptorSet Set = {
		Device,
		Configuration,
		StringTable,
		Bos,
		VendorRequests,
		USBBuiltParts<Make>.serialNumberAtRuntime,
		RuntimeStringIndexes
	};


	static_assert(USBDeviceClassMatchesConfiguration(Device, Configuration), "the device class does not match the configuration's interface associations");
	static_assert(USBDeviceBosMatches(Device, Bos), "a device with a BOS announces bcdUSB 0x0201 or later");


	// The function's layout: its interface numbers, its endpoint addresses in the order its class requested them, and its slice
	// of the configuration descriptor (its interface association first, when it has one). consteval: the call is evaluated at
	// compile time, so a call with an index that is not a constant is a compile error and cannot reach the link.
	static consteval USBFunctionLayout Function(std::size_t index) {
		Require<USBText{"function index is outside the configuration"}>(index < USBBuiltParts<Make>.functionCount);
		const USBFunctionRecord& record = USBBuiltParts<Make>.functions[index];
		return USBFunctionLayout{
			std::span<const uint8>(InterfaceNumbers).subspan(record.firstInterface, record.interfaceCount),
			std::span<const uint8>(EndpointAddresses).subspan(record.endpointStart, record.endpointCount),
			std::span<const uint8>(Configuration).subspan(record.descriptorOffset, record.descriptorSize),
			std::span<const uint8>(RuntimeStringIndexes).subspan(record.runtimeStart, record.runtimeCount)
		};
	}
};
