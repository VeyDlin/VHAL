#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorString.h>
#include <Drivers/Interface/USB/USBProtocol.h>


class USBDeviceBuilder;


// What the configuration keeps for one function: where its interfaces and endpoints are, and where its descriptors are in
// the configuration's body (offsets from the first byte after the configuration header). Its run-time strings are the ones
// from runtimeStart on, counted over the whole configuration.
struct USBFunctionRecord {
	uint8 firstInterface = 0;
	std::size_t interfaceCount = 0;
	std::size_t endpointStart = 0;
	std::size_t endpointCount = 0;
	std::size_t descriptorOffset = 0;
	std::size_t descriptorSize = 0;
	bool winUsb = false;
	std::string_view guid;
	std::size_t runtimeStart = 0;
	std::size_t runtimeCount = 0;
};


// Returns the 1-based index of text in table, adding it when it is new. Equal texts share one index, so a string is numbered in
// order of its first use. count is the number of entries in use.
constexpr std::size_t USBInternString(std::array<std::string_view, USBMaxStrings>& table, std::size_t& count, std::string_view text) {
	Require<"USB: a string is not empty">(!text.empty());
	USBUtf16Count units = USBCountUtf16Units(text.data(), text.size());
	Require<"USB: string is not valid UTF-8">(units.valid);
	Require<"USB: a string descriptor holds at most 126 UTF-16 code units">(units.units <= 126);
	for (std::size_t index = 0; index < count; index++) {
		if (table[index] == text) {
			return index + 1;
		}
	}

	Require<"USB: too many strings in one device">(count < USBMaxStrings);
	table[count] = text;
	count++;
	return count;
}


// The configuration of the device: bus power, the functions in the order they were added, and the descriptor bytes they
// produce. Interface numbers, endpoint addresses and local string indexes are assigned as each function is added, so the
// functions are written in the order of the chain. The device joins the configuration to its strings and limits.
class USBConfigurationBuilder {
private:
	friend class USBDeviceBuilder;


	USBBuildBuffer body;
	USBPatchList patches;
	// The run-time string indexes the functions write. Each entry's local is the run-time string's position in the configuration's
	// list; the device turns it into a string index when it joins the configuration.
	USBPatchList runtimePatches;
	std::size_t runtimeStringCount = 0;
	std::array<std::string_view, USBMaxStrings> strings{};
	std::size_t stringCount = 0;
	std::array<USBFunctionRecord, USBMaxFunctions> functions{};
	std::size_t functionCount = 0;
	std::array<uint8, USBMaxEndpoints> endpointAddresses{};
	std::size_t endpointAddressCount = 0;
	std::array<bool, 32> addressUsed{};
	std::size_t interfaceCount = 0;
	uint8 highestEndpointNumber = 0;
	bool associated = false;
	bool maxPowerSet = false;
	uint8 maxPower = 0;
	bool selfPowered = false;
	bool remoteWakeup = false;


public:
	// Maximum bus current in mA, a multiple of 2 up to 500 (bMaxPower counts 2 mA). Required.
	constexpr USBConfigurationBuilder& MaxPowerMilliamps(uint16 milliamps) {
		Require<"USB: bMaxPower is at most 500 mA">(milliamps <= 500);
		Require<"USB: bMaxPower counts 2 mA steps, so the value is even">(milliamps % 2 == 0);
		maxPower = static_cast<uint8>(milliamps / 2);
		maxPowerSet = true;
		return *this;
	}


	constexpr USBConfigurationBuilder& SelfPowered() {
		selfPowered = true;
		return *this;
	}


	constexpr USBConfigurationBuilder& RemoteWakeup() {
		remoteWakeup = true;
		return *this;
	}


	// Adds a function: resolves its interfaces, endpoints and strings, writes its interface association when it has two or more
	// interfaces, and lets the function emit its descriptors. The function's descriptor slice starts with that association.
	template<USBClassFunctionBuilder FunctionType>
	constexpr USBConfigurationBuilder& Function(const FunctionType& function) {
		std::size_t count = function.InterfaceCount();
		Require<"USB: a function has at least one interface">(count >= 1);
		Require<"USB: interface numbers are one byte">(interfaceCount + count <= 256);
		Require<"USB: too many functions in one configuration">(functionCount < USBMaxFunctions);

		USBFunctionSlot slot;
		slot.firstInterface = static_cast<uint8>(interfaceCount);
		slot.interfaceCount = count;
		slot.stringCount = function.StringCount();
		Require<"USB: a function names at most 4 strings">(slot.stringCount <= USBMaxFunctionStrings);
		for (std::size_t index = 0; index < slot.stringCount; index++) {
			slot.strings[index] = RegisterString(function.String(index));
		}

		slot.endpointCount = function.EndpointCount();
		Require<"USB: a function has at most 16 endpoints">(slot.endpointCount <= USBMaxFunctionEndpoints);
		for (std::size_t index = 0; index < slot.endpointCount; index++) {
			slot.requests[index] = function.Endpoint(index);
			slot.addresses[index] = ResolveEndpoint(slot.requests[index]);
		}

		slot.runtimeStart = runtimeStringCount;
		slot.runtimeCount = USBRuntimeStringCount(function);
		Require<"USB: too many run-time strings in one configuration">(runtimeStringCount + slot.runtimeCount <= USBMaxRuntimeStrings);
		runtimeStringCount += slot.runtimeCount;

		USBFunctionRecord record;
		record.firstInterface = slot.firstInterface;
		record.interfaceCount = count;
		record.endpointStart = endpointAddressCount;
		record.endpointCount = slot.endpointCount;
		record.runtimeStart = slot.runtimeStart;
		record.runtimeCount = slot.runtimeCount;
		for (std::size_t index = 0; index < slot.endpointCount; index++) {
			Require<"USB: too many endpoints in one configuration">(endpointAddressCount < USBMaxEndpoints);
			endpointAddresses[endpointAddressCount] = slot.addresses[index];
			endpointAddressCount++;
		}

		record.descriptorOffset = body.size;
		if (count >= 2) {
			body.Append(MakeUSBInterfaceAssociationDescriptor(
				slot.firstInterface,
				static_cast<uint8>(count),
				function.FunctionClass(),
				function.FunctionSubClass(),
				function.FunctionProtocol(),
				0
			));
			associated = true;
		}

		USBFunctionEmitter emitter(body, patches, runtimePatches, slot);
		function.Emit(emitter);
		record.descriptorSize = body.size - record.descriptorOffset;
		record.winUsb = function.HasWinUsb();
		record.guid = function.WinUsbGuid();
		Require<"USB: a WinUSB GUID is given with WinUsb()">(record.guid.empty() || record.winUsb);
		Require<"USB: a WinUSB GUID is {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} in braces">(record.guid.empty() || USBGuidIsValid(record.guid));
		functions[functionCount] = record;
		functionCount++;
		interfaceCount += count;
		return *this;
	}


private:
	// Local string index of text in this configuration (1-based). The device renumbers it when it joins the configuration.
	constexpr std::size_t RegisterString(std::string_view text) {
		return USBInternString(strings, stringCount, text);
	}


	// The address of one endpoint request: the explicit address as given, or the smallest number that neither direction uses
	// yet, from 1. A number above the device's limit is rejected when the configuration joins the device.
	constexpr uint8 ResolveEndpoint(const USBEndpointRequest& request) {
		Require<"USB: endpoints are bulk, interrupt or isochronous">(
			request.type == USBEndpointType::Bulk
			|| request.type == USBEndpointType::Interrupt
			|| request.type == USBEndpointType::Isochronous);
		bool in = request.direction == USBDirection::In;
		if (request.address != 0) {
			uint8 address = request.address;
			Require<"USB: endpoint address bits 6..4 are reserved">(USBEndpoint::IsValid(address));
			Require<"USB: endpoint direction does not match the endpoint request">(USBEndpoint::IsIn(address) == in);
			Require<"USB: endpoint 0 belongs to the control pipe">(USBEndpoint::Number(address) != 0);
			Require<"USB: duplicate endpoint address">(!addressUsed[EndpointSlot(address)]);
			addressUsed[EndpointSlot(address)] = true;
			highestEndpointNumber = Max(highestEndpointNumber, USBEndpoint::Number(address));
			return address;
		}

		for (uint8 number = 1; number <= USBEndpoint::MaxNumber; number++) {
			uint8 candidate = in ? USBEndpoint::MakeIn(number) : USBEndpoint::MakeOut(number);
			bool inUse = addressUsed[EndpointSlot(USBEndpoint::MakeIn(number))] || addressUsed[EndpointSlot(USBEndpoint::MakeOut(number))];
			if (!inUse) {
				addressUsed[EndpointSlot(candidate)] = true;
				highestEndpointNumber = Max(highestEndpointNumber, number);
				return candidate;
			}
		}

		Require<"USB: no endpoint number is free">(false);
		return 0;
	}


	// Index into addressUsed: the number for OUT, 16 more for IN.
	static constexpr std::size_t EndpointSlot(uint8 address) {
		return (USBEndpoint::IsIn(address) ? 16 : 0) + USBEndpoint::Number(address);
	}


	static constexpr uint8 Max(uint8 left, uint8 right) {
		return left > right ? left : right;
	}
};
