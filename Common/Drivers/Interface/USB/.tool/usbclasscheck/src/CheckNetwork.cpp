#include "CheckNetwork.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <winsock2.h>
#include <windows.h>

#include "CheckData.h"
#include "DemoProducts.h"
#include "DeviceIdentity.h"
#include "IcmpEcho.h"
#include "NetworkAdapters.h"
#include "NetworkLogic.h"
#include "Report.h"

namespace {

// The lease comes a few seconds after the board's ACK, and the address is preferred once duplicate address detection has run. The wait
// is bounded, so a board that never leases fails the check instead of hanging it.
constexpr DWORD LeaseWaitMs = 30000;
constexpr DWORD LeasePollMs = 500;
// An echo over the link answers in a few milliseconds; the timeout only stops an echo that never comes back.
constexpr std::uint32_t EchoTimeoutMs = 2000;
// The echoes sent for each payload size.
constexpr std::uint32_t EchoesPerSize = 4;


// Reads the adapter table and picks the adapter that has the host MAC. False with the reason in problem when there is no single one.
bool FindAdapter(const MacAddress& hostMac, AdapterInfo& adapter, std::string& problem) {
	std::vector<AdapterInfo> adapters;
	if (!EnumerateAdapters(adapters, problem)) {
		return false;
	}

	std::size_t index = 0;
	AdapterSelection selection = SelectAdapter(adapters, hostMac, index);
	if (selection == AdapterSelection::NotFound) {
		problem = "no network adapter has the MAC " + MacText(hostMac.data(), hostMac.size()) + ": the board's adapter is not present";
		return false;
	}

	if (selection == AdapterSelection::Several) {
		problem = "several network adapters have the MAC " + MacText(hostMac.data(), hostMac.size());
		return false;
	}

	adapter = adapters[index];
	return true;
}


// Reads the adapter table and picks the adapter that has the host MAC. Adds an error step and returns false when there is no single one.
bool LookUpAdapter(const MacAddress& hostMac, AdapterInfo& adapter, CheckReport& report) {
	std::string problem;
	if (!FindAdapter(hostMac, adapter, problem)) {
		report.AddStep("adapter", StepStatus::Error, problem);
		return false;
	}

	return true;
}


// Waits, up to LeaseWaitMs, until the adapter has the leased address preferred. The adapter is read again on every poll, because its
// state changes while the link comes up. When the adapter cannot be read during the wait, the lease step fails with the reason. The
// adapter step has already passed, so no second adapter step is added: that would look like a second check.
bool WaitForLease(const MacAddress& hostMac, AdapterInfo& adapter, CheckReport& report) {
	ULONGLONG deadline = GetTickCount64() + LeaseWaitMs;
	while (!LeaseReady(adapter) && GetTickCount64() < deadline) {
		Sleep(LeasePollMs);
		std::string problem;
		if (!FindAdapter(hostMac, adapter, problem)) {
			report.AddStep("lease address", StepStatus::Fail, "the adapter could not be read during the lease wait: " + problem);
			return false;
		}
	}

	return true;
}


// Sends the echoes of each payload size through the leased address and adds one step per size.
void RunEchoes(std::uint32_t sourceAddress, CheckReport& report) {
	EchoSender sender;
	std::string error;
	if (!sender.Open(error)) {
		report.AddStep("ping", StepStatus::Error, error);
		return;
	}

	// The payload is a new pattern for every echo, so a reply that echoes an old request cannot pass as this one.
	std::random_device entropy;
	for (std::size_t payloadBytes : EchoPayloadSizes) {
		std::vector<std::uint8_t> payload(payloadBytes);
		EchoSizeSummary summary;
		summary.payloadBytes = payloadBytes;
		for (std::uint32_t echo = 1; echo <= EchoesPerSize; echo++) {
			FillPattern(payload, entropy());
			EchoResult result = sender.Send(sourceAddress, DemoProducts::NetworkDeviceAddress, payload, EchoTimeoutMs);
			std::string problem = EchoProblem(payload, DemoProducts::NetworkDeviceAddress, result);
			AddEchoOutcome(summary, echo, result, problem);
		}

		AddEchoStep(summary, report);
	}
}


// Reads the neighbour table and checks the board's entry on the adapter's interface.
void RunArpCheck(std::uint32_t interfaceIndex, const MacAddress& deviceMac, CheckReport& report) {
	std::vector<ArpEntry> entries;
	std::string error;
	if (!EnumerateArpEntries(entries, error)) {
		report.AddStep("ARP entry", StepStatus::Error, error);
		return;
	}

	EvaluateArp(entries, interfaceIndex, DemoProducts::NetworkDeviceAddress, deviceMac, report);
}


// The adapter's description line for the report: its name, driver description and link speeds.
std::string AdapterText(const AdapterInfo& adapter) {
	std::string driver = adapter.description.empty() ? "driver description not reported" : adapter.description;
	return adapter.name + ", " + driver + ", link receive " + LinkSpeedText(adapter.receiveBitsPerSecond) + " and transmit "
		+ LinkSpeedText(adapter.transmitBitsPerSecond);
}

}


void RunNetworkCheck(const BoardInfo& board, CheckReport& report) {
	// Without the serial the board's MACs cannot be derived, so there is no adapter to look for.
	if (board.serial.empty()) {
		report.AddStep("MAC", StepStatus::Error, "the board has no serial number, so its MAC addresses cannot be derived");
		return;
	}

	NetworkMacs macs;
	std::string reason;
	if (!DeriveNetworkMacs(board.serial, macs, reason)) {
		report.AddStep("MAC", StepStatus::Error, reason);
		return;
	}

	report.AddStep(
		"MAC",
		StepStatus::Pass,
		"host " + MacText(macs.hostMac.data(), macs.hostMac.size()) + ", board " + MacText(macs.deviceMac.data(), macs.deviceMac.size()));

	AdapterInfo adapter;
	if (!LookUpAdapter(macs.hostMac, adapter, report)) {
		return;
	}

	report.AddStep("adapter", StepStatus::Pass, AdapterText(adapter));
	if (!WaitForLease(macs.hostMac, adapter, report)) {
		return;
	}

	if (!adapter.up) {
		report.AddStep("adapter up", StepStatus::Fail, "the adapter is not up after the wait");
		return;
	}

	report.AddStep("adapter up", StepStatus::Pass, "the adapter is up");
	if (!LeaseReady(adapter)) {
		report.AddStep(
			"lease address",
			StepStatus::Fail,
			"no preferred " + Ipv4Text(DemoProducts::NetworkLeasedAddress) + " within 30 s; the adapter has " + AddressesText(adapter.addresses));
		return;
	}

	report.AddStep("lease address", StepStatus::Pass, Ipv4Text(DemoProducts::NetworkLeasedAddress) + " preferred");
	EvaluateLease(adapter, report);
	RunEchoes(DemoProducts::NetworkLeasedAddress, report);
	RunArpCheck(adapter.interfaceIndex, macs.deviceMac, report);
}
