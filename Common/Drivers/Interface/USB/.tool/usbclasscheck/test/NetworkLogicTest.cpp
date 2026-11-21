#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "NetworkLogic.h"
#include "Report.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


std::string MacOf(const MacAddress& mac) {
	return MacText(mac.data(), mac.size());
}


// The step of the report with this name, or nullptr when there is none.
const CheckStep* FindStep(const CheckReport& report, const std::string& name) {
	for (const CheckStep& step : report.Steps()) {
		if (step.name == name) {
			return &step;
		}
	}

	return nullptr;
}


// An adapter that has the lease, with the given prefix length and state of duplicate address detection, and no gateway.
AdapterInfo LeasedAdapter(std::uint8_t prefixLength, bool preferred) {
	AdapterInfo adapter;
	adapter.name = "Ethernet 3";
	adapter.up = true;
	AdapterAddress address;
	address.address = 0xC0A80702;
	address.prefixLength = prefixLength;
	address.preferred = preferred;
	adapter.addresses.push_back(address);
	return adapter;
}


void CheckDeriveRule() {
	NetworkMacs macs;
	std::string reason;

	// Unique ID bytes 01 02 03 04 05 06 | 07 08 09 0A 0B 0C. The host takes the first six: 01 has bit 1 clear, so it becomes 02 (bit 1 set,
	// bit 0 clear) and the rest stay. The board takes the last six: 07 is 0000 0111, clearing bit 0 gives 0000 0110, bit 1 is set already.
	Expect(DeriveNetworkMacs("010203040506" "0708090A0B0C", macs, reason), "a 24 hex digit serial derives the MACs");
	Expect(MacOf(macs.hostMac) == "02:02:03:04:05:06", "the host MAC is the first six bytes, locally administered");
	Expect(MacOf(macs.deviceMac) == "06:08:09:0A:0B:0C", "the board MAC is the last six bytes, locally administered");

	// Unique ID bytes FF 00 00 00 00 00 | 00 00 00 00 00 FF. FF is 1111 1111, clearing bit 0 gives FE. The board's 00 becomes 02.
	Expect(DeriveNetworkMacs("FF0000000000" "0000000000FF", macs, reason), "a serial of FF and zero bytes derives the MACs");
	Expect(MacOf(macs.hostMac) == "FE:00:00:00:00:00", "the host's first byte FF becomes FE");
	Expect(MacOf(macs.deviceMac) == "02:00:00:00:00:FF", "the board's first byte 00 becomes 02 and its last byte is kept");

	// Both halves 02 AA BB CC DD EE. The two MACs would be equal, so the board's last byte gets 1 added: EE becomes EF.
	Expect(DeriveNetworkMacs("02AABBCCDDEE" "02AABBCCDDEE", macs, reason), "a serial whose halves are equal derives the MACs");
	Expect(MacOf(macs.hostMac) == "02:AA:BB:CC:DD:EE", "the host MAC of equal halves is kept");
	Expect(MacOf(macs.deviceMac) == "02:AA:BB:CC:DD:EF", "the board MAC of equal halves gets 1 added to its last byte");

	Expect(DeriveNetworkMacs("010203040506" "0708090a0b0c", macs, reason), "lower case hex is accepted");
	Expect(MacOf(macs.deviceMac) == "06:08:09:0A:0B:0C", "lower case hex gives the same MAC");

	Expect(!DeriveNetworkMacs("0102030405", macs, reason), "a short serial is refused");
	Expect(!DeriveNetworkMacs("010203040506" "0708090A0B0G", macs, reason), "a serial with a character that is not hex is refused");
	Expect(!DeriveNetworkMacs("(no serial number)", macs, reason), "a board with no serial text is refused");
	Expect(!DeriveNetworkMacs("", macs, reason), "an empty serial is refused");
	Expect(!DeriveNetworkMacs("010203040506" "0708090A0B0C0D", macs, reason), "a serial longer than the unique ID is refused");
}


void CheckText() {
	Expect(Ipv4Text(0xC0A80702) == "192.168.7.2", "an IPv4 address prints in dotted form");
	Expect(Ipv4Text(0xFFFFFF00) == "255.255.255.0", "a mask prints in dotted form");

	const std::uint8_t mac[] = {0x02, 0xAA, 0xBB, 0x0C, 0xDD, 0xEE};
	Expect(MacText(mac, 6) == "02:AA:BB:0C:DD:EE", "a MAC prints as upper-case colon-separated hex");

	Expect(LinkSpeedText(0) == "unknown", "a zero link speed is unknown");
	Expect(LinkSpeedText(0xFFFFFFFFFFFFFFFFULL) == "unknown", "the all-ones link speed is unknown");
	Expect(LinkSpeedText(480000000) == "480 Mb/s", "a 480 Mb/s link prints in megabits");
	Expect(LinkSpeedText(1000000000) == "1000 Mb/s", "a gigabit link prints as 1000 Mb/s");
	Expect(LinkSpeedText(500000) == "500000 b/s", "a speed below one megabit prints in bits");

	std::vector<AdapterAddress> none;
	Expect(AddressesText(none) == "none", "an adapter with no address says none");
	AdapterAddress first;
	first.address = 0xC0A80702;
	first.prefixLength = 24;
	AdapterAddress second;
	second.address = 0xC0A80701;
	second.prefixLength = 24;
	std::vector<AdapterAddress> two = {first, second};
	Expect(AddressesText(two) == "192.168.7.2, 192.168.7.1", "addresses are listed in the order Windows gives them");
}


void CheckMask() {
	std::uint32_t mask = 0;
	Expect(MaskOfPrefix(24, mask) && mask == 0xFFFFFF00, "a prefix of 24 is the mask 255.255.255.0");
	Expect(MaskOfPrefix(23, mask) && mask == 0xFFFFFE00, "a prefix of 23 is the mask 255.255.254.0");
	Expect(MaskOfPrefix(0, mask) && mask == 0, "a prefix of 0 is the mask 0.0.0.0");
	Expect(MaskOfPrefix(32, mask) && mask == 0xFFFFFFFF, "a prefix of 32 is the mask 255.255.255.255");
	Expect(!MaskOfPrefix(33, mask), "a prefix above 32 is refused");
}


void CheckSelection() {
	MacAddress host = {0x02, 0x02, 0x03, 0x04, 0x05, 0x06};
	MacAddress other = {0x02, 0x02, 0x03, 0x04, 0x05, 0x07};

	AdapterInfo mine;
	mine.name = "Ethernet 3";
	mine.mac = {0x02, 0x02, 0x03, 0x04, 0x05, 0x06};
	AdapterInfo foreign;
	foreign.name = "Ethernet";
	foreign.mac = {0x02, 0x02, 0x03, 0x04, 0x05, 0x07};
	AdapterInfo copy = mine;
	copy.name = "Ethernet 4";

	std::size_t index = 9;
	std::vector<AdapterInfo> beside = {foreign, mine};
	Expect(SelectAdapter(beside, host, index) == AdapterSelection::Chosen && index == 1, "the adapter with the host MAC is chosen");

	std::vector<AdapterInfo> twins = {mine, copy};
	Expect(SelectAdapter(twins, host, index) == AdapterSelection::Several, "two adapters with the host MAC are refused");

	std::vector<AdapterInfo> without = {foreign};
	Expect(SelectAdapter(without, host, index) == AdapterSelection::NotFound, "no adapter with the host MAC is not found");
	Expect(SelectAdapter(without, other, index) == AdapterSelection::Chosen && index == 0, "the MAC is compared byte for byte");

	std::vector<AdapterInfo> none;
	Expect(SelectAdapter(none, host, index) == AdapterSelection::NotFound, "an empty adapter list is not found");
}


void CheckLease() {
	Expect(LeaseReady(LeasedAdapter(24, true)), "an up adapter with the preferred lease is ready");
	Expect(!LeaseReady(LeasedAdapter(24, false)), "a lease that is still in duplicate address detection is not ready");

	AdapterInfo down = LeasedAdapter(24, true);
	down.up = false;
	Expect(!LeaseReady(down), "a lease on a down adapter is not ready");

	AdapterInfo otherAddress = LeasedAdapter(24, true);
	otherAddress.addresses[0].address = 0xC0A80703;
	Expect(!LeaseReady(otherAddress), "an adapter without the leased address is not ready");

	CheckReport good("network");
	EvaluateLease(LeasedAdapter(24, true), good);
	Expect(FindStep(good, "lease mask") != nullptr && FindStep(good, "lease mask")->status == StepStatus::Pass, "the demo's mask passes");
	Expect(FindStep(good, "lease gateway") != nullptr && FindStep(good, "lease gateway")->status == StepStatus::Pass, "a lease with no router passes");
	Expect(good.Verdict() == StepStatus::Pass, "a lease with the demo's mask and no router passes");

	CheckReport wideMask("network");
	EvaluateLease(LeasedAdapter(23, true), wideMask);
	const CheckStep* mask = FindStep(wideMask, "lease mask");
	Expect(mask != nullptr && mask->status == StepStatus::Fail, "a mask other than the demo's fails");
	Expect(mask != nullptr && mask->detail == "the lease gave 255.255.254.0, the demo gives 255.255.255.0", "the mask failure names both masks");

	AdapterInfo routed = LeasedAdapter(24, true);
	routed.gateways.push_back(0xC0A80701);
	CheckReport withRouter("network");
	EvaluateLease(routed, withRouter);
	const CheckStep* gateway = FindStep(withRouter, "lease gateway");
	Expect(gateway != nullptr && gateway->status == StepStatus::Fail, "a lease that names a router fails");
	Expect(withRouter.Verdict() == StepStatus::Fail, "the verdict of a lease with a router is fail");
}


void CheckEchoProblems() {
	std::vector<std::uint8_t> sent = {1, 2, 3, 4};

	EchoResult good;
	good.replied = true;
	good.source = 0xC0A80701;
	good.status = 0;
	good.roundTripMs = 1;
	good.data = {1, 2, 3, 4};
	Expect(EchoProblem(sent, 0xC0A80701, good).empty(), "a reply from the board with the same bytes matches");

	EchoResult silent;
	Expect(EchoProblem(sent, 0xC0A80701, silent) == "no reply", "no reply is a problem");

	EchoResult unsent;
	unsent.problem = "the request was not sent: example";
	Expect(EchoProblem(sent, 0xC0A80701, unsent) == "the request was not sent: example", "a request that was not sent keeps its reason");

	EchoResult stranger = good;
	stranger.source = 0xC0A80702;
	Expect(EchoProblem(sent, 0xC0A80701, stranger) == "the reply came from 192.168.7.2", "a reply from another address is a problem");

	EchoResult failed = good;
	failed.status = 11;
	Expect(EchoProblem(sent, 0xC0A80701, failed) == "the reply has status 11", "a reply with a status is a problem");

	EchoResult shortReply = good;
	shortReply.data = {1, 2, 3};
	Expect(EchoProblem(sent, 0xC0A80701, shortReply) == "the reply has 3 bytes, the request 4", "a reply with fewer bytes is a problem");

	EchoResult changed = good;
	changed.data = {1, 2, 9, 4};
	Expect(EchoProblem(sent, 0xC0A80701, changed) == "the reply's data differs at byte 2", "a reply with other bytes names the first byte");

	std::vector<std::uint8_t> empty;
	EchoResult emptyEcho = good;
	emptyEcho.data.clear();
	Expect(EchoProblem(empty, 0xC0A80701, emptyEcho).empty(), "an empty echo with no data matches");
}


void CheckRtt() {
	RttStats stats;
	AddRtt(stats, 1);
	AddRtt(stats, 2);
	AddRtt(stats, 4);
	Expect(RttText(stats) == "RTT min 1 ms, avg 2.3 ms, max 4 ms", "seven over three is 2.3 ms, rounded to a tenth");

	RttStats pair;
	AddRtt(pair, 1);
	AddRtt(pair, 2);
	Expect(RttText(pair) == "RTT min 1 ms, avg 1.5 ms, max 2 ms", "one and two average 1.5 ms");

	RttStats none;
	Expect(RttText(none) == "no RTT", "no replies give no RTT");
}


void CheckEchoSteps() {
	EchoResult answered;
	answered.replied = true;
	answered.roundTripMs = 1;

	EchoSizeSummary partial;
	partial.payloadBytes = 32;
	EchoResult second = answered;
	second.roundTripMs = 2;
	EchoResult third = answered;
	third.roundTripMs = 4;
	EchoResult silent;
	AddEchoOutcome(partial, 1, answered, "");
	AddEchoOutcome(partial, 2, second, "");
	AddEchoOutcome(partial, 3, third, "");
	AddEchoOutcome(partial, 4, silent, "no reply");

	CheckReport partialReport("network");
	AddEchoStep(partial, partialReport);
	const CheckStep* partialStep = FindStep(partialReport, "ping 32 bytes");
	Expect(partialStep != nullptr && partialStep->status == StepStatus::Fail, "a size with a missing reply fails");
	Expect(
		partialStep != nullptr && partialStep->detail
			== "3 of 4 echoes, RTT min 1 ms, avg 2.3 ms, max 4 ms; first problem, echo 4: no reply",
		"the failed size names the count, the RTTs and the first problem");

	EchoSizeSummary full;
	full.payloadBytes = 0;
	for (std::uint32_t echo = 1; echo <= 4; echo++) {
		AddEchoOutcome(full, echo, answered, "");
	}

	CheckReport fullReport("network");
	AddEchoStep(full, fullReport);
	const CheckStep* fullStep = FindStep(fullReport, "ping 0 bytes");
	Expect(fullStep != nullptr && fullStep->status == StepStatus::Pass, "a size with every reply matching passes");
	Expect(fullStep != nullptr && fullStep->detail == "4 of 4 echoes, RTT min 1 ms, avg 1.0 ms, max 1 ms", "the passing size gives its count and RTTs");
}


void CheckEchoSizes() {
	const std::size_t expected[] = {0, 32, 1000, 1472};
	const std::size_t count = sizeof(EchoPayloadSizes) / sizeof(EchoPayloadSizes[0]);
	Expect(count == 4, "four payload sizes are pinged");
	bool same = count == 4;
	for (std::size_t index = 0; same && index < count; index++) {
		same = EchoPayloadSizes[index] == expected[index];
	}

	Expect(same, "the payload sizes are 0, 32, 1000 and 1472 bytes");
}


void CheckArp() {
	MacAddress boardMac = {0x06, 0x08, 0x09, 0x0A, 0x0B, 0x0C};

	ArpEntry entry;
	entry.interfaceIndex = 7;
	entry.address = 0xC0A80701;
	entry.mac = {0x06, 0x08, 0x09, 0x0A, 0x0B, 0x0C};

	std::vector<ArpEntry> entries = {entry};
	CheckReport matching("network");
	EvaluateArp(entries, 7, 0xC0A80701, boardMac, matching);
	const CheckStep* matchStep = FindStep(matching, "ARP entry");
	Expect(matchStep != nullptr && matchStep->status == StepStatus::Pass, "the board's entry on the adapter with its MAC passes");
	Expect(matchStep != nullptr && matchStep->detail == "192.168.7.1 is at 06:08:09:0A:0B:0C", "the passing entry names the address and the MAC");

	ArpEntry wrongMac = entry;
	wrongMac.mac = {0x06, 0x08, 0x09, 0x0A, 0x0B, 0x0D};
	std::vector<ArpEntry> wrong = {wrongMac};
	CheckReport differs("network");
	EvaluateArp(wrong, 7, 0xC0A80701, boardMac, differs);
	Expect(differs.Verdict() == StepStatus::Fail, "an entry with another MAC fails");

	CheckReport otherAdapter("network");
	EvaluateArp(entries, 8, 0xC0A80701, boardMac, otherAdapter);
	Expect(otherAdapter.Verdict() == StepStatus::Fail, "an entry on another adapter does not count");

	ArpEntry unresolved = entry;
	unresolved.mac.clear();
	std::vector<ArpEntry> incomplete = {unresolved};
	CheckReport noMac("network");
	EvaluateArp(incomplete, 7, 0xC0A80701, boardMac, noMac);
	Expect(noMac.Verdict() == StepStatus::Fail, "an entry with no resolved MAC fails");

	CheckReport missing("network");
	EvaluateArp(std::vector<ArpEntry>(), 7, 0xC0A80701, boardMac, missing);
	Expect(missing.Verdict() == StepStatus::Fail, "a board with no ARP entry fails");
}


}


int main() {
	CheckDeriveRule();
	CheckText();
	CheckMask();
	CheckSelection();
	CheckLease();
	CheckEchoProblems();
	CheckRtt();
	CheckEchoSteps();
	CheckEchoSizes();
	CheckArp();

	std::printf("usbclasscheck network logic test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
