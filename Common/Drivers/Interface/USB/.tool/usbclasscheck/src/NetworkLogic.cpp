#include "NetworkLogic.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "CheckData.h"
#include "DemoProducts.h"
#include "Report.h"
#include "Text.h"

namespace {

// The unique ID is 12 bytes, so the serial number is 24 hex digits.
constexpr std::size_t UniqueIdBytes = 12;
constexpr std::size_t MacBytes = 6;
// The status of an echo reply that succeeded (IP_SUCCESS).
constexpr std::uint32_t IpSuccess = 0;
// Windows reports a link speed of all ones when it does not know the speed.
constexpr std::uint64_t UnknownSpeed = 0xFFFFFFFFFFFFFFFFULL;
constexpr std::uint64_t BitsPerMegabit = 1000000;


// The value of one hex digit, or -1 when the character is not one.
int HexDigitValue(char digit) {
	if (digit >= '0' && digit <= '9') {
		return digit - '0';
	}

	if (digit >= 'A' && digit <= 'F') {
		return digit - 'A' + 10;
	}

	if (digit >= 'a' && digit <= 'f') {
		return digit - 'a' + 10;
	}

	return -1;
}


bool MacEquals(const std::vector<std::uint8_t>& mac, const MacAddress& expected) {
	if (mac.size() != expected.size()) {
		return false;
	}

	for (std::size_t index = 0; index < expected.size(); index++) {
		if (mac[index] != expected[index]) {
			return false;
		}
	}

	return true;
}


// The lease's address in the adapter's list, or nullptr when the adapter has no such address.
const AdapterAddress* FindLeasedAddress(const AdapterInfo& adapter) {
	for (const AdapterAddress& address : adapter.addresses) {
		if (address.address == DemoProducts::NetworkLeasedAddress) {
			return &address;
		}
	}

	return nullptr;
}


std::string NumberText(std::uint64_t value) {
	return std::to_string(value);
}

}


bool DeriveNetworkMacs(const std::string& serial, NetworkMacs& macs, std::string& reason) {
	if (serial.size() != UniqueIdBytes * 2) {
		reason = "the serial number is " + NumberText(serial.size()) + " characters, the unique ID is 24 hex digits";
		return false;
	}

	std::array<std::uint8_t, UniqueIdBytes> uniqueId {};
	for (std::size_t index = 0; index < UniqueIdBytes; index++) {
		int high = HexDigitValue(serial[index * 2]);
		int low = HexDigitValue(serial[index * 2 + 1]);
		if (high < 0 || low < 0) {
			reason = "the serial number has a character that is not a hex digit";
			return false;
		}

		uniqueId[index] = static_cast<std::uint8_t>(high * 16 + low);
	}

	DemoProducts::NetworkMacsFromUniqueId(uniqueId.data(), macs.hostMac.data(), macs.deviceMac.data());
	return true;
}


std::string MacText(const std::uint8_t* mac, std::size_t length) {
	std::string text;
	for (std::size_t index = 0; index < length; index++) {
		if (index > 0) {
			text += ":";
		}

		AppendFormat(text, "%02X", static_cast<unsigned int>(mac[index]));
	}

	return text;
}


std::string Ipv4Text(std::uint32_t address) {
	std::string text;
	AppendFormat(
		text,
		"%u.%u.%u.%u",
		static_cast<unsigned int>((address >> 24) & 0xFF),
		static_cast<unsigned int>((address >> 16) & 0xFF),
		static_cast<unsigned int>((address >> 8) & 0xFF),
		static_cast<unsigned int>(address & 0xFF));
	return text;
}


bool MaskOfPrefix(std::uint8_t prefixLength, std::uint32_t& mask) {
	if (prefixLength > 32) {
		return false;
	}

	// A shift by 32 is not defined, so the empty prefix is a mask of zero on its own.
	mask = prefixLength == 0 ? 0 : static_cast<std::uint32_t>(0xFFFFFFFFULL << (32 - prefixLength));
	return true;
}


std::string LinkSpeedText(std::uint64_t bitsPerSecond) {
	if (bitsPerSecond == 0 || bitsPerSecond == UnknownSpeed) {
		return "unknown";
	}

	if (bitsPerSecond < BitsPerMegabit) {
		return NumberText(bitsPerSecond) + " b/s";
	}

	return NumberText(bitsPerSecond / BitsPerMegabit) + " Mb/s";
}


AdapterSelection SelectAdapter(const std::vector<AdapterInfo>& adapters, const MacAddress& hostMac, std::size_t& index) {
	bool found = false;
	for (std::size_t position = 0; position < adapters.size(); position++) {
		if (!MacEquals(adapters[position].mac, hostMac)) {
			continue;
		}

		if (found) {
			return AdapterSelection::Several;
		}

		found = true;
		index = position;
	}

	return found ? AdapterSelection::Chosen : AdapterSelection::NotFound;
}


std::string AddressesText(const std::vector<AdapterAddress>& addresses) {
	if (addresses.empty()) {
		return "none";
	}

	std::string text;
	for (const AdapterAddress& address : addresses) {
		if (!text.empty()) {
			text += ", ";
		}

		text += Ipv4Text(address.address);
	}

	return text;
}


bool LeaseReady(const AdapterInfo& adapter) {
	if (!adapter.up) {
		return false;
	}

	const AdapterAddress* leased = FindLeasedAddress(adapter);
	return leased != nullptr && leased->preferred;
}


void EvaluateLease(const AdapterInfo& adapter, CheckReport& report) {
	const AdapterAddress* leased = FindLeasedAddress(adapter);
	if (leased == nullptr) {
		report.AddStep("lease mask", StepStatus::Fail, "the adapter has no " + Ipv4Text(DemoProducts::NetworkLeasedAddress));
	} else {
		std::uint32_t mask = 0;
		if (!MaskOfPrefix(leased->prefixLength, mask)) {
			report.AddStep("lease mask", StepStatus::Fail, "the prefix length " + NumberText(leased->prefixLength) + " is not valid");
		} else if (mask != DemoProducts::NetworkNetmask) {
			report.AddStep(
				"lease mask",
				StepStatus::Fail,
				"the lease gave " + Ipv4Text(mask) + ", the demo gives " + Ipv4Text(DemoProducts::NetworkNetmask));
		} else {
			report.AddStep("lease mask", StepStatus::Pass, Ipv4Text(mask));
		}
	}

	if (adapter.gateways.empty()) {
		report.AddStep("lease gateway", StepStatus::Pass, "none, the lease names no router");
	} else {
		report.AddStep(
			"lease gateway",
			StepStatus::Fail,
			"the lease gave " + Ipv4Text(adapter.gateways[0]) + ", the demo gives none");
	}
}


std::string EchoProblem(const std::vector<std::uint8_t>& sent, std::uint32_t deviceAddress, const EchoResult& result) {
	if (!result.replied) {
		return result.problem.empty() ? "no reply" : result.problem;
	}

	if (result.source != deviceAddress) {
		return "the reply came from " + Ipv4Text(result.source);
	}

	if (result.status != IpSuccess) {
		return "the reply has status " + NumberText(result.status);
	}

	if (result.data.size() != sent.size()) {
		return "the reply has " + NumberText(result.data.size()) + " bytes, the request " + NumberText(sent.size());
	}

	std::size_t offset = 0;
	if (FirstMismatch(sent, result.data, offset)) {
		return "the reply's data differs at byte " + NumberText(offset);
	}

	return "";
}


void AddRtt(RttStats& stats, std::uint32_t roundTripMs) {
	if (stats.count == 0) {
		stats.minimum = roundTripMs;
		stats.maximum = roundTripMs;
	} else {
		if (roundTripMs < stats.minimum) {
			stats.minimum = roundTripMs;
		}

		if (roundTripMs > stats.maximum) {
			stats.maximum = roundTripMs;
		}
	}

	stats.count++;
	stats.total += roundTripMs;
}


std::string RttText(const RttStats& stats) {
	if (stats.count == 0) {
		return "no RTT";
	}

	// The average in tenths of a millisecond, rounded half up, so 7 over 3 replies prints as 2.3.
	std::uint64_t tenths = (stats.total * 10 + stats.count / 2) / stats.count;
	return "RTT min " + NumberText(stats.minimum) + " ms, avg " + NumberText(tenths / 10) + "." + NumberText(tenths % 10)
		+ " ms, max " + NumberText(stats.maximum) + " ms";
}


void AddEchoOutcome(EchoSizeSummary& summary, std::uint32_t echoNumber, const EchoResult& result, const std::string& problem) {
	summary.sent++;
	if (problem.empty()) {
		summary.matched++;
		AddRtt(summary.rtt, result.roundTripMs);
		return;
	}

	if (summary.firstProblem.empty()) {
		summary.firstProblem = "echo " + NumberText(echoNumber) + ": " + problem;
	}
}


void AddEchoStep(const EchoSizeSummary& summary, CheckReport& report) {
	std::string name = "ping " + NumberText(summary.payloadBytes) + " bytes";
	std::string detail = NumberText(summary.matched) + " of " + NumberText(summary.sent) + " echoes, " + RttText(summary.rtt);
	if (summary.sent > 0 && summary.matched == summary.sent) {
		report.AddStep(name, StepStatus::Pass, detail);
		return;
	}

	report.AddStep(name, StepStatus::Fail, detail + "; first problem, " + summary.firstProblem);
}


void EvaluateArp(
	const std::vector<ArpEntry>& entries,
	std::uint32_t interfaceIndex,
	std::uint32_t deviceAddress,
	const MacAddress& deviceMac,
	CheckReport& report)
{
	const ArpEntry* found = nullptr;
	for (const ArpEntry& entry : entries) {
		if (entry.interfaceIndex == interfaceIndex && entry.address == deviceAddress) {
			found = &entry;
			break;
		}
	}

	if (found == nullptr) {
		report.AddStep("ARP entry", StepStatus::Fail, "no entry for " + Ipv4Text(deviceAddress) + " on the adapter");
		return;
	}

	if (found->mac.size() != MacBytes) {
		report.AddStep("ARP entry", StepStatus::Fail, Ipv4Text(deviceAddress) + " has no resolved MAC address");
		return;
	}

	std::string entryMac = MacText(found->mac.data(), found->mac.size());
	if (!MacEquals(found->mac, deviceMac)) {
		report.AddStep(
			"ARP entry",
			StepStatus::Fail,
			Ipv4Text(deviceAddress) + " is at " + entryMac + ", the board's MAC is " + MacText(deviceMac.data(), deviceMac.size()));
		return;
	}

	report.AddStep("ARP entry", StepStatus::Pass, Ipv4Text(deviceAddress) + " is at " + entryMac);
}
