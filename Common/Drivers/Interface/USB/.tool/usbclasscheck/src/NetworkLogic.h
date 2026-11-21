#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Report.h"

// The MAC address of an adapter or of the board, as its six bytes.
using MacAddress = std::array<std::uint8_t, 6>;

// The two MACs of a network demo board: the host's adapter (iMACAddress) and the board's own address.
struct NetworkMacs {
	MacAddress hostMac {};
	MacAddress deviceMac {};
};

// Derives the two MACs from the board's serial number, which is the hex text of its 12-byte unique ID (24 hex digits, either case).
// The rule is DemoProducts::NetworkMacsFromUniqueId, the same one the firmware uses. False with the reason when the serial is not one.
bool DeriveNetworkMacs(const std::string& serial, NetworkMacs& macs, std::string& reason);

// The MAC as colon-separated upper-case hex, such as 02:AA:BB:CC:DD:EE.
std::string MacText(const std::uint8_t* mac, std::size_t length);

// An IPv4 address, the first octet in the most significant byte, in dotted form.
std::string Ipv4Text(std::uint32_t address);

// The mask of a prefix length. False when the length is above 32.
bool MaskOfPrefix(std::uint8_t prefixLength, std::uint32_t& mask);

// The link speed in bits per second as text, such as "480 Mb/s". Zero and the all-ones value Windows uses for an unknown speed
// print as "unknown".
std::string LinkSpeedText(std::uint64_t bitsPerSecond);


// One IPv4 address of an adapter, with its prefix length and whether duplicate address detection has finished (preferred).
struct AdapterAddress {
	std::uint32_t address = 0;
	std::uint8_t prefixLength = 0;
	bool preferred = false;
};

// One network adapter as Windows reports it. NetworkAdapters.cpp fills it in; the matching and the lease checks are here.
struct AdapterInfo {
	std::uint32_t interfaceIndex = 0;
	std::string name;
	std::string description;
	std::vector<std::uint8_t> mac;
	bool up = false;
	std::uint64_t transmitBitsPerSecond = 0;
	std::uint64_t receiveBitsPerSecond = 0;
	std::vector<AdapterAddress> addresses;
	std::vector<std::uint32_t> gateways;
};

enum class AdapterSelection : std::uint8_t {
	Chosen,
	NotFound,
	Several
};

// Picks the adapter whose MAC is the board's host MAC. index is set when the result is Chosen. Several means two adapters carry the
// MAC, so the board cannot be told apart from the other one and the check does not guess.
AdapterSelection SelectAdapter(const std::vector<AdapterInfo>& adapters, const MacAddress& hostMac, std::size_t& index);

// The addresses of an adapter, dotted and separated by commas, or "none".
std::string AddressesText(const std::vector<AdapterAddress>& addresses);

// True when the adapter is up and has the address the board leases, and duplicate address detection has finished on it.
bool LeaseReady(const AdapterInfo& adapter);

// Adds the mask and gateway steps for the lease the adapter has. The mask must be the demo's netmask. The demo's lease names no router,
// so a gateway is a failure. Call it once LeaseReady holds.
void EvaluateLease(const AdapterInfo& adapter, CheckReport& report);


// The payload sizes of the ping, in bytes: none, a short one, one that spans many bytes, and the largest that fits the 1500-byte IPv4
// MTU behind the 28 bytes of IPv4 and ICMP headers.
inline constexpr std::size_t EchoPayloadSizes[] = {0, 32, 1000, 1472};


// What one echo request got back. replied is false when no reply came in time, or when the request could not be sent; problem says why.
struct EchoResult {
	bool replied = false;
	std::uint32_t source = 0;
	std::uint32_t status = 0;
	std::uint32_t roundTripMs = 0;
	std::vector<std::uint8_t> data;
	std::string problem;
};

// The reason the reply is not the one the request asked for: a missing reply, another source, a status other than success, or data
// that differs from what was sent. Empty when the reply is right.
std::string EchoProblem(const std::vector<std::uint8_t>& sent, std::uint32_t deviceAddress, const EchoResult& result);


// The round trip times of the matching replies of one payload size: the count, the sum for the average, and the extremes.
struct RttStats {
	std::uint32_t count = 0;
	std::uint64_t total = 0;
	std::uint32_t minimum = 0;
	std::uint32_t maximum = 0;
};

void AddRtt(RttStats& stats, std::uint32_t roundTripMs);

// "RTT min 1 ms, avg 2.3 ms, max 4 ms", with the average to a tenth of a millisecond, or "no RTT" when there are no replies.
std::string RttText(const RttStats& stats);


// The echoes sent for one payload size: how many were sent and matched, the first problem and the round trip times of the matches.
struct EchoSizeSummary {
	std::size_t payloadBytes = 0;
	std::uint32_t sent = 0;
	std::uint32_t matched = 0;
	std::string firstProblem;
	RttStats rtt;
};

// Adds the outcome of echo number echoNumber (counted from 1). An empty problem means the reply matched.
void AddEchoOutcome(EchoSizeSummary& summary, std::uint32_t echoNumber, const EchoResult& result, const std::string& problem);

// Adds the step for one payload size, named "ping N bytes". It passes when every echo matched.
void AddEchoStep(const EchoSizeSummary& summary, CheckReport& report);


// One entry of the neighbour table: the interface it is on, the IPv4 address and the MAC (empty for an entry that is not resolved).
struct ArpEntry {
	std::uint32_t interfaceIndex = 0;
	std::uint32_t address = 0;
	std::vector<std::uint8_t> mac;
};

// Adds the ARP step: the board's address must have an entry on the adapter's interface, with the board's MAC.
void EvaluateArp(
	const std::vector<ArpEntry>& entries,
	std::uint32_t interfaceIndex,
	std::uint32_t deviceAddress,
	const MacAddress& deviceMac,
	CheckReport& report);
