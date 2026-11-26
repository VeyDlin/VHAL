#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20DescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20PlatformCapability.h>
#include <Drivers/Interface/USB/Descriptor/USBBosDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/WebUsb/USBWebUsbPlatformCapability.h>
#include <Drivers/Interface/USB/Descriptor/WebUsb/USBWebUsbUrlDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorString.h>
#include <Drivers/Interface/USB/Descriptor/USBDeviceDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>


// Every byte the chain produces, each part in a buffer of capacity USBBuildCapacity. USBDescriptors copies each part into an
// array of its exact size; the sizes come from these buffers, so no part is sized by hand.
struct USBDescriptorParts {
	std::array<uint8, 18> device{};
	USBBuildBuffer configuration;
	USBBuildBuffer strings;
	// Start of each string descriptor in strings; entry n answers string index n, and the entry after the last one is the end.
	std::array<std::size_t, USBMaxStrings + 2> stringOffsets{};
	std::size_t stringCount = 0;
	USBBuildBuffer bos;
	USBBuildBuffer msOs20Set;
	bool msOs20 = false;
	uint8 msOs20VendorCode = 0;
	bool webUsb = false;
	uint8 webUsbVendorCode = 0;
	USBBuildBuffer webUsbUrl;
	bool serialNumberAtRuntime = false;
	// String index of each run-time string, in the order the classes reserved them. Each follows the fixed strings.
	std::array<uint8, USBMaxRuntimeStrings> runtimeStringIndexes{};
	std::size_t runtimeStringIndexCount = 0;
	std::array<USBFunctionRecord, USBMaxFunctions> functions{};
	std::size_t functionCount = 0;
	std::array<uint8, USBMaxEndpoints> endpointAddresses{};
	std::size_t endpointAddressCount = 0;
	std::size_t interfaceCount = 0;
};


// Windows version the MS OS 2.0 set asks for: 0x06030000 is Windows 8.1, the first release that reads the descriptors.
constexpr uint32 USBMsOs20WindowsVersion = 0x06030000;


// Registry name of the WinUSB interface GUID property, and the sizes of the registry property that carries it.
constexpr std::size_t USBGuidNameUnits = USBUtf16UnitCount<"DeviceInterfaceGUIDs">();


// Data of the DeviceInterfaceGUIDs property: the GUID with its terminator, then one more terminator (REG_MULTI_SZ).
constexpr std::size_t USBGuidDataSize(std::string_view guid) {
	return 2 * (USBCountUtf16Units(guid.data(), guid.size()).units + 1) + 2;
}


// A registry property feature: 10 bytes of header, the name with its terminator, then the data.
constexpr std::size_t USBGuidPropertySize(std::string_view guid) {
	return 10 + 2 * (USBGuidNameUnits + 1) + USBGuidDataSize(guid);
}


// The features of one WinUSB function: the compatible ID, and the GUID property when a GUID is given.
constexpr std::size_t USBWinUsbFeaturesSize(std::string_view guid) {
	return 20 + (guid.empty() ? 0 : USBGuidPropertySize(guid));
}


// Appends the DeviceInterfaceGUIDs registry property for guid.
constexpr void USBAppendGuidProperty(USBBuildBuffer& buffer, std::string_view guid) {
	constexpr std::size_t nameBytes = 2 * (USBGuidNameUnits + 1);
	buffer.Put16(static_cast<uint16>(USBGuidPropertySize(guid)));
	buffer.Put16(static_cast<uint16>(USBMsOs20DescriptorType::RegistryProperty));
	buffer.Put16(static_cast<uint16>(USBMsOs20PropertyType::MultiString));
	buffer.Put16(static_cast<uint16>(nameBytes));
	buffer.Append(MakeUSBUtf16LENullTerminated<"DeviceInterfaceGUIDs">());
	buffer.Put16(static_cast<uint16>(USBGuidDataSize(guid)));
	buffer.size = USBEncodeUtf16(guid.data(), guid.size(), buffer.bytes, buffer.size);
	buffer.Put16(0);
	buffer.Put16(0);
}


// The fluent builder of the device descriptor and everything that joins it: strings, the configuration, the BOS and the MS OS
// 2.0 set. Every identity is required and has no default. Chain it inside a lambda given to USBDescriptors.
class USBDeviceBuilder {
private:
	bool vendorSet = false;
	bool productSet = false;
	bool releaseSet = false;
	bool manufacturerSet = false;
	bool productNameSet = false;
	uint16 vendorId = 0;
	uint16 productId = 0;
	uint16 releaseNumber = 0;
	std::array<std::string_view, USBMaxStrings> strings{};
	std::size_t stringCount = 0;
	std::size_t manufacturerIndex = 0;
	std::size_t productIndex = 0;
	std::size_t serialIndex = 0;
	bool serialSet = false;
	bool serialRuntime = false;
	uint16 languageId = 0x0409;
	uint8 endpointLimit = USBEndpoint::MaxNumber;
	bool usbVersionSet = false;
	uint16 usbVersion = 0;
	bool usb2Extension = false;
	bool vendorCodeSet = false;
	uint8 vendorCode = 0;
	bool webUsbSet = false;
	uint8 webUsbVendorCode = 0;
	std::string_view webUsbLandingPage;
	bool configurationSet = false;
	USBConfigurationBuilder configuration;


public:
	constexpr USBDeviceBuilder& VendorId(uint16 value) {
		vendorId = value;
		vendorSet = true;
		return *this;
	}


	constexpr USBDeviceBuilder& ProductId(uint16 value) {
		productId = value;
		productSet = true;
		return *this;
	}


	// bcdDevice, the device release number.
	constexpr USBDeviceBuilder& DeviceRelease(uint16 value) {
		releaseNumber = value;
		releaseSet = true;
		return *this;
	}


	// bcdUSB. Without it the builder picks 0x0200, or 0x0210 when a BOS is built. A BOS needs 0x0201 or later.
	constexpr USBDeviceBuilder& UsbVersion(uint16 value) {
		usbVersion = value;
		usbVersionSet = true;
		return *this;
	}


	// Adds the USB 2.0 extension capability to the BOS. It is off by default, so a device that asks for no BOS has none.
	constexpr USBDeviceBuilder& Usb2Extension() {
		usb2Extension = true;
		return *this;
	}


	// The LANGID of the string descriptor 0. Defaults to 0x0409 (English, United States). Zero names no language.
	constexpr USBDeviceBuilder& Language(uint16 value) {
		Require<"USB:language ID 0 names no language">(value != 0);
		languageId = value;
		return *this;
	}


	// Highest endpoint number the device uses, 1 to 15. Some controllers have fewer endpoints; the default is the USB limit.
	constexpr USBDeviceBuilder& EndpointNumberLimit(uint8 value) {
		Require<"USB:EndpointNumberLimit is 1 to 15">(value >= 1 && value <= USBEndpoint::MaxNumber);
		endpointLimit = value;
		return *this;
	}


	constexpr USBDeviceBuilder& Manufacturer(std::string_view text) {
		manufacturerIndex = USBInternString(strings, stringCount, text);
		manufacturerSet = true;
		return *this;
	}


	constexpr USBDeviceBuilder& Product(std::string_view text) {
		productIndex = USBInternString(strings, stringCount, text);
		productNameSet = true;
		return *this;
	}


	constexpr USBDeviceBuilder& SerialNumber(std::string_view text) {
		Require<"USB:the serial number is given once">(!serialSet && !serialRuntime);
		serialIndex = USBInternString(strings, stringCount, text);
		serialSet = true;
		return *this;
	}


	// Reserves iSerialNumber with an empty placeholder string. The application supplies the real descriptor before Start().
	constexpr USBDeviceBuilder& SerialNumberAtRuntime() {
		Require<"USB:the serial number is given once">(!serialSet && !serialRuntime);
		Require<"USB:too many strings in one device">(stringCount < USBMaxStrings);
		strings[stringCount] = std::string_view();
		stringCount++;
		serialIndex = stringCount;
		serialRuntime = true;
		return *this;
	}


	// The bMS_VendorCode of the MS OS 2.0 descriptors. Required when a function asks for WinUSB.
	constexpr USBDeviceBuilder& MsOs20VendorCode(uint8 code) {
		vendorCode = code;
		vendorCodeSet = true;
		return *this;
	}


	// Adds the WebUSB platform capability to the BOS and answers GET_URL with landingPage, the URL a browser opens for the device.
	// The vendor code is the bRequest of GET_URL. The URL may start with https:// or http://; any other text is sent without a
	// scheme. The builder requires bcdUSB 0x0210 or later for WebUSB, the USB 2.1 version that Chrome's guide gives for the BOS. The
	// WebUSB specification itself sets no minimum.
	constexpr USBDeviceBuilder& WebUsb(uint8 code, std::string_view landingPage) {
		Require<"USB:WebUsb is given once">(!webUsbSet);
		Require<"USB:WebUsb needs a landing page URL">(!landingPage.empty());
		webUsbVendorCode = code;
		webUsbLandingPage = landingPage;
		webUsbSet = true;
		return *this;
	}


	// Joins a configuration: its strings are numbered after the device's, its string references are rewritten to the global
	// numbers, and its functions and endpoints are kept. Exactly one configuration is allowed.
	constexpr USBDeviceBuilder& Configuration(const USBConfigurationBuilder& source) {
		Require<"USB:a device has one configuration">(!configurationSet);
		Require<"USB:a configuration has a maximum power">(source.maxPowerSet);
		Require<"USB:a configuration has at least one interface">(source.interfaceCount >= 1);
		configuration = source;
		configurationSet = true;

		// A run-time string that no descriptor carries could never be read by the host, so it is refused.
		for (std::size_t runtime = 0; runtime < source.runtimeStringCount; runtime++) {
			Require<"USB:a run-time string is written into a descriptor">(RuntimeStringWritten(source, runtime));
		}

		std::array<std::size_t, USBMaxStrings + 1> mapping{};
		for (std::size_t local = 1; local <= source.stringCount; local++) {
			mapping[local] = USBInternString(strings, stringCount, source.strings[local - 1]);
		}

		for (std::size_t index = 0; index < configuration.patches.count; index++) {
			const USBStringPatch& patch = configuration.patches.entries[index];
			configuration.body.bytes[patch.offset] = static_cast<uint8>(mapping[patch.local]);
		}

		return *this;
	}


	// Checks the rules that join the parts and builds every part. USBDescriptors calls this once, in a constant expression.
	constexpr USBDescriptorParts Build() const {
		Require<"USB:VendorId is required">(vendorSet);
		Require<"USB:ProductId is required">(productSet);
		Require<"USB:DeviceRelease is required">(releaseSet);
		Require<"USB:Manufacturer is required">(manufacturerSet);
		Require<"USB:Product is required">(productNameSet);
		Require<"USB:SerialNumber or SerialNumberAtRuntime is required">(serialSet || serialRuntime);
		Require<"USB:a device has a configuration">(configurationSet);
		Require<"USB:an endpoint number is above EndpointNumberLimit">(configuration.highestEndpointNumber <= endpointLimit);

		std::size_t winUsbCount = 0;
		std::size_t winUsbFeatures = 0;
		for (std::size_t index = 0; index < configuration.functionCount; index++) {
			const USBFunctionRecord& record = configuration.functions[index];
			if (record.winUsb) {
				winUsbCount++;
				winUsbFeatures += USBWinUsbFeaturesSize(record.guid);
			}
		}

		Require<"USB:MsOs20VendorCode is required when a function asks for WinUSB">(winUsbCount == 0 || vendorCodeSet);
		Require<"USB:MsOs20VendorCode needs a function that asks for WinUSB">(!vendorCodeSet || winUsbCount > 0);

		bool bos = vendorCodeSet || usb2Extension || webUsbSet;
		uint16 bcdUSB = usbVersion;
		if (!usbVersionSet) {
			bcdUSB = bos ? 0x0210 : 0x0200;
		}

		Require<"USB:WebUsb needs bcdUSB 0x0210 or later">(!webUsbSet || bcdUSB >= 0x0210);
		Require<"USB:a device with a BOS has bcdUSB 0x0201 or later">(!bos || bcdUSB >= 0x0201);

		bool associated = configuration.associated;
		USBDescriptorParts parts;
		parts.device = MakeUSBDeviceDescriptor(
			bcdUSB,
			associated ? static_cast<uint8>(USBClassCode::Miscellaneous) : 0x00,
			associated ? 0x02 : 0x00,
			associated ? 0x01 : 0x00,
			static_cast<uint8>(USBDeviceAdapter::ControlMaxPacketSize),
			vendorId,
			productId,
			releaseNumber,
			static_cast<uint8>(manufacturerIndex),
			static_cast<uint8>(productIndex),
			static_cast<uint8>(serialIndex),
			1
		);
		BuildConfiguration(parts);
		BuildStrings(parts);
		parts.serialNumberAtRuntime = serialRuntime;
		parts.runtimeStringIndexCount = configuration.runtimeStringCount;
		for (std::size_t index = 0; index < configuration.runtimeStringCount; index++) {
			parts.runtimeStringIndexes[index] = RuntimeStringIndex(index);
		}

		parts.msOs20 = vendorCodeSet;
		parts.msOs20VendorCode = vendorCode;
		parts.webUsb = webUsbSet;
		parts.webUsbVendorCode = webUsbVendorCode;
		if (bos) {
			BuildBos(parts, winUsbFeatures);
		}

		for (std::size_t index = 0; index < configuration.functionCount; index++) {
			USBFunctionRecord record = configuration.functions[index];
			record.descriptorOffset += 9;
			parts.functions[index] = record;
		}

		parts.functionCount = configuration.functionCount;
		parts.endpointAddressCount = configuration.endpointAddressCount;
		for (std::size_t index = 0; index < configuration.endpointAddressCount; index++) {
			parts.endpointAddresses[index] = configuration.endpointAddresses[index];
		}

		parts.interfaceCount = configuration.interfaceCount;
		return parts;
	}


private:
	// True when some descriptor of the configuration carries the run-time string with the given position.
	static constexpr bool RuntimeStringWritten(const USBConfigurationBuilder& source, std::size_t runtime) {
		for (std::size_t index = 0; index < source.runtimePatches.count; index++) {
			if (source.runtimePatches.entries[index].local == runtime) {
				return true;
			}
		}

		return false;
	}


	// String index of the run-time string with the given position. The fixed strings are 1 to stringCount, so the run-time strings
	// follow them in the order they were reserved.
	constexpr uint8 RuntimeStringIndex(std::size_t runtime) const {
		return static_cast<uint8>(stringCount + 1 + runtime);
	}


	// The configuration header (USB 2.0 section 9.6.3) and the body the configuration kept. bNumEndpoints of each interface is
	// set when the bytes are checked in USBDescriptors, where the array has its exact size. The run-time string indexes are
	// written into a copy of the body here, because only now are the fixed strings all numbered.
	constexpr void BuildConfiguration(USBDescriptorParts& parts) const {
		USBBuildBuffer body = configuration.body;
		for (std::size_t index = 0; index < configuration.runtimePatches.count; index++) {
			const USBStringPatch& patch = configuration.runtimePatches.entries[index];
			body.bytes[patch.offset] = RuntimeStringIndex(patch.local);
		}

		std::size_t total = 9 + body.size;
		Require<"USB:bMaxPower is at most 500 mA">(configuration.maxPower <= 250);
		uint8 attributes = 0x80;
		if (configuration.selfPowered) {
			attributes |= 0x40;
		}

		if (configuration.remoteWakeup) {
			attributes |= 0x20;
		}

		parts.configuration.Put(9);
		parts.configuration.Put(static_cast<uint8>(USBDescriptorType::Configuration));
		parts.configuration.Put16(static_cast<uint16>(total));
		parts.configuration.Put(static_cast<uint8>(configuration.interfaceCount));
		parts.configuration.Put(1);
		parts.configuration.Put(0);
		parts.configuration.Put(attributes);
		parts.configuration.Put(configuration.maxPower);
		parts.configuration.Append(std::span<const uint8>(body.bytes).first(body.size));
	}


	// The language descriptor, then the device's strings in the order they were numbered, then the run-time placeholders. Each
	// placeholder is an empty string descriptor until SetRuntimeString() replaces it before Start().
	constexpr void BuildStrings(USBDescriptorParts& parts) const {
		Require<"USB:too many strings in one device">(stringCount + configuration.runtimeStringCount <= USBMaxStrings);
		parts.stringOffsets[0] = 0;
		parts.strings.Append(MakeUSBLanguageIdDescriptor(languageId));
		for (std::size_t index = 0; index < stringCount; index++) {
			parts.stringOffsets[index + 1] = parts.strings.size;
			USBBuildStringDescriptor(strings[index], parts.strings);
		}

		for (std::size_t index = 0; index < configuration.runtimeStringCount; index++) {
			parts.stringOffsets[stringCount + 1 + index] = parts.strings.size;
			USBBuildStringDescriptor(std::string_view(), parts.strings);
		}

		std::size_t count = stringCount + configuration.runtimeStringCount;
		parts.stringOffsets[count + 1] = parts.strings.size;
		parts.stringCount = count + 1;
	}


	// The BOS (USB 3.x section 9.6.2): the USB 2.0 extension when asked for, then the MS OS 2.0 platform capability when a function
	// asks for WinUSB. The MS OS 2.0 set is built first, because the capability carries its length.
	constexpr void BuildBos(USBDescriptorParts& parts, std::size_t winUsbFeatures) const {
		std::size_t capabilities = usb2Extension ? 1 : 0;
		std::size_t total = usb2Extension ? 5 + 7 : 5;
		std::size_t setSize = 0;
		if (vendorCodeSet) {
			setSize = BuildMsOs20Set(parts, winUsbFeatures);
			capabilities++;
			total += 28;
		}

		if (webUsbSet) {
			USBAppendWebUsbUrlDescriptor(parts.webUsbUrl, webUsbLandingPage);
			capabilities++;
			total += 24;
		}

		parts.bos.Put(5);
		parts.bos.Put(static_cast<uint8>(USBDescriptorType::BOS));
		parts.bos.Put16(static_cast<uint16>(total));
		parts.bos.Put(static_cast<uint8>(capabilities));
		if (usb2Extension) {
			parts.bos.Append(MakeUSBUsb2ExtensionCapability(false));
		}

		if (vendorCodeSet) {
			std::array<uint8, 8> data{};
			USBDescriptorPut32(data, 0, USBMsOs20WindowsVersion);
			USBDescriptorPut16(data, 4, static_cast<uint16>(setSize));
			data[6] = vendorCode;
			data[7] = 0;
			parts.bos.Append(MakeUSBPlatformCapability(USBMsOs20PlatformUuid, data));
		}

		if (webUsbSet) {
			parts.bos.Append(MakeWebUsbPlatformCapability(webUsbVendorCode, USBWebUsbLandingPageIndex));
		}
	}


	// The MS OS 2.0 descriptor set: a device-level set for one interface, else one function subset per WinUSB function. Returns
	// the size of the set.
	constexpr std::size_t BuildMsOs20Set(USBDescriptorParts& parts, std::size_t winUsbFeatures) const {
		bool deviceLevel = configuration.interfaceCount == 1;
		std::size_t setSize = 10 + winUsbFeatures;
		if (!deviceLevel) {
			setSize += 8 * BuiltWinUsbFunctions();
		}

		Require<"USB:the MS OS 2.0 set is at most 65535 bytes">(setSize <= 0xFFFF);
		parts.msOs20Set.Put16(10);
		parts.msOs20Set.Put16(static_cast<uint16>(USBMsOs20DescriptorType::SetHeader));
		parts.msOs20Set.Put16(static_cast<uint16>(USBMsOs20WindowsVersion & 0xFFFF));
		parts.msOs20Set.Put16(static_cast<uint16>(USBMsOs20WindowsVersion >> 16));
		parts.msOs20Set.Put16(static_cast<uint16>(setSize));
		for (std::size_t index = 0; index < configuration.functionCount; index++) {
			const USBFunctionRecord& record = configuration.functions[index];
			if (!record.winUsb) {
				continue;
			}

			if (!deviceLevel) {
				parts.msOs20Set.Put16(8);
				parts.msOs20Set.Put16(static_cast<uint16>(USBMsOs20DescriptorType::FunctionSubsetHeader));
				parts.msOs20Set.Put(record.firstInterface);
				parts.msOs20Set.Put(0);
				parts.msOs20Set.Put16(static_cast<uint16>(8 + USBWinUsbFeaturesSize(record.guid)));
			}

			parts.msOs20Set.Append(MakeMsOs20CompatibleId("WINUSB", ""));
			if (!record.guid.empty()) {
				USBAppendGuidProperty(parts.msOs20Set, record.guid);
			}
		}

		return setSize;
	}


	// Number of functions that ask for WinUSB, for the subset headers of a multi-interface set.
	constexpr std::size_t BuiltWinUsbFunctions() const {
		std::size_t count = 0;
		for (std::size_t index = 0; index < configuration.functionCount; index++) {
			if (configuration.functions[index].winUsb) {
				count++;
			}
		}

		return count;
	}
};
