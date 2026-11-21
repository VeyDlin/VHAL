#include <VHAL.h>
#include "SimAssert.h"
#include <DemoProducts.h>
#include <DemoUSBNetwork/Network/Arp.h>
#include <DemoUSBNetwork/Network/Dhcp.h>
#include <DemoUSBNetwork/Network/EthernetFrame.h>
#include <DemoUSBNetwork/Network/IcmpEcho.h>
#include <DemoUSBNetwork/Network/InternetChecksum.h>
#include <DemoUSBNetwork/Network/Ipv4Packet.h>
#include <DemoUSBNetwork/Network/NetworkResponder.h>
#include <DemoUSBNetwork/Network/UdpPacket.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <span>
#include <vector>


// The pure network code of the network demo, checked with packets written by hand from the RFCs: RFC 1071 (the checksum), RFC 826 (ARP),
// RFC 792 (ICMP), RFC 791 and 768 (IPv4 and UDP), RFC 2131 and 2132 (DHCP). Every expected byte is written here from the specification,
// not taken from the code's output. The checksums the packets carry are worked out by hand, and each is noted where it is used. A
// packet with a changed byte that needs a new checksum has the new checksum written by hand too.
namespace {


using Bytes = std::vector<uint8>;


// The device and the host, as the test packets name them. The device MAC is locally administered (02), like every derived MAC.
const Bytes HostMac = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
const Bytes DeviceMac = { 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
const Bytes Broadcast6 = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
const Bytes Ip192_168_7_1 = { 0xC0, 0xA8, 0x07, 0x01 };
const Bytes Ip192_168_7_2 = { 0xC0, 0xA8, 0x07, 0x02 };
const Bytes Ip192_168_7_5 = { 0xC0, 0xA8, 0x07, 0x05 };
const Bytes Ip192_168_7_9 = { 0xC0, 0xA8, 0x07, 0x09 };
const Bytes Ip192_168_7_50 = { 0xC0, 0xA8, 0x07, 0x32 };
const Bytes Ip0 = { 0x00, 0x00, 0x00, 0x00 };
const Bytes Ip255 = { 0xFF, 0xFF, 0xFF, 0xFF };
const Bytes DhcpCookie = { 0x63, 0x82, 0x53, 0x63 };


// The device's identity as the responder is given it: the device MAC above, 192.168.7.1, and the lease 192.168.7.2.
DemoNetwork::NetworkIdentity Identity() {
	DemoNetwork::NetworkIdentity identity;
	identity.mac = { 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
	identity.address = 0xC0A80701;
	identity.leasedAddress = 0xC0A80702;
	return identity;
}


// The constants the demo takes from DemoProducts are the values the packets in this file name.
static_assert(DemoProducts::NetworkDeviceAddress == 0xC0A80701, "the device answers on 192.168.7.1");
static_assert(DemoProducts::NetworkLeasedAddress == 0xC0A80702, "the lease is 192.168.7.2");
static_assert(DemoProducts::NetworkNetmask == 0xFFFFFF00, "the netmask is 255.255.255.0");
static_assert(DemoProducts::NetworkProductId == 0x0005, "the network demo's PID is 0005");


// Joins the parts into one packet, in order.
Bytes Join(std::initializer_list<Bytes> parts) {
	Bytes out;
	for (const Bytes& part : parts) {
		out.insert(out.end(), part.begin(), part.end());
	}

	return out;
}


Bytes Zeros(std::size_t count) {
	return Bytes(count, 0);
}


// A copy of base with values written at offset.
Bytes With(Bytes base, std::size_t offset, std::initializer_list<uint8> values) {
	std::size_t index = offset;
	for (uint8 value : values) {
		base[index] = value;
		index++;
	}

	return base;
}


// The first difference between actual and expected, for the failure message. The bytes at maskFrom to maskTo (exclusive) are not compared:
// they are checksums that the test checks in another way.
void ExpectBytesExcept(
	std::span<const uint8> actual,
	const Bytes& expected,
	std::size_t maskFrom,
	std::size_t maskTo,
	const char* name,
	const char* file,
	int line)
{
	bool same = actual.size() == expected.size();
	std::size_t firstDifference = 0;
	if (same) {
		for (std::size_t index = 0; index < expected.size(); index++) {
			if (index >= maskFrom && index < maskTo) {
				continue;
			}

			if (actual[index] != expected[index]) {
				same = false;
				firstDifference = index;
				break;
			}
		}
	}

	if (!same) {
		std::fprintf(
			stderr,
			"%s:%d: %s differs: %zu bytes against %zu, first difference at %zu\n",
			file,
			line,
			name,
			actual.size(),
			expected.size(),
			firstDifference);
	}

	SimTest::Expect(same, name, file, line);
}


#define NET_EXPECT_BYTES(actual, expected) ExpectBytesExcept((actual), (expected), 0, 0, #actual, __FILE__, __LINE__)
#define NET_EXPECT_BYTES_EXCEPT(actual, expected, from, to) ExpectBytesExcept((actual), (expected), (from), (to), #actual, __FILE__, __LINE__)
#define NET_EXPECT(condition) SimTest::Expect(static_cast<bool>(condition), #condition, __FILE__, __LINE__)
#define NET_EXPECT_EQ(actual, expected) SimTest::ExpectEqual((actual), (expected), #actual, __FILE__, __LINE__)


// The answer to one frame, with the counters it leaves behind. Each test starts from zero counters.
struct Answer {
	std::size_t length = 0;
	DemoNetwork::NetworkCounters counters{};
	Bytes reply;
};


Answer AnswerTo(const Bytes& frame) {
	Answer answer;
	std::array<uint8, DemoNetwork::EthernetMaxFrameSize> reply{};
	answer.length = DemoNetwork::AnswerFrame(frame, reply, Identity(), answer.counters);
	answer.reply.assign(reply.begin(), reply.begin() + static_cast<std::ptrdiff_t>(answer.length));
	return answer;
}


// A frame gets no answer: the reply is empty, it is counted as dropped, and as malformed when expectMalformed says it broke a rule.
void ExpectNoAnswer(const Bytes& frame, bool expectMalformed) {
	Answer answer = AnswerTo(frame);
	NET_EXPECT_EQ(answer.length, 0);
	NET_EXPECT_EQ(answer.counters.dropped, 1);
	NET_EXPECT_EQ(answer.counters.malformed, expectMalformed ? 1 : 0);
	NET_EXPECT_EQ(answer.counters.arpReplies + answer.counters.icmpReplies + answer.counters.dhcpOffers + answer.counters.dhcpAcks, 0);
}


// ---- Checksums ----


// The RFC 1071 example: the words 0001 F203 F4F5 F6F7 sum to DDF2 and the checksum is 220D.
void TestChecksumRfcExample() {
	const Bytes words = { 0x00, 0x01, 0xF2, 0x03, 0xF4, 0xF5, 0xF6, 0xF7 };
	NET_EXPECT_EQ(DemoNetwork::OnesComplementSum(words, 0), 0xDDF2);
	NET_EXPECT_EQ(DemoNetwork::InternetChecksum(words, 0), 0x220D);
}


// An odd last byte is the high byte of a word: 01 is the word 0100, whose complement is FEFF.
void TestChecksumOddLength() {
	const Bytes odd = { 0x01 };
	NET_EXPECT_EQ(DemoNetwork::OnesComplementSum(odd, 0), 0x0100);
	NET_EXPECT_EQ(DemoNetwork::InternetChecksum(odd, 0), 0xFEFF);
}


// The initial value is added in: 0001 with an initial 0002 sums to 0003.
void TestChecksumInitialValue() {
	const Bytes word = { 0x00, 0x01 };
	NET_EXPECT_EQ(DemoNetwork::OnesComplementSum(word, 0x0002), 0x0003);
	NET_EXPECT_EQ(DemoNetwork::OnesComplementSum(word, 0xFFFF), 0x0001);
}


// ---- MAC derivation ----


// The UID 00 11 22 33 44 55 A0 B1 C2 D3 E4 F5: the host takes the first six bytes with the first byte's flags set (00 becomes 02),
// the device the last six (A0 becomes A2). The two differ, so nothing is adjusted.
constexpr std::array<uint8, 12> UidA = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0xA0, 0xB1, 0xC2, 0xD3, 0xE4, 0xF5 };
constexpr std::array<uint8, 6> HostMacA = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
constexpr std::array<uint8, 6> DeviceMacA = { 0xA2, 0xB1, 0xC2, 0xD3, 0xE4, 0xF5 };


// Twelve bytes of 10: the two halves are equal, so the device's last byte gets 1 added: 10 becomes 12 for the first byte (its flags),
// and the last byte 10 becomes 11.
constexpr std::array<uint8, 12> UidEqual = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10 };
constexpr std::array<uint8, 6> HostMacEqual = { 0x12, 0x10, 0x10, 0x10, 0x10, 0x10 };
constexpr std::array<uint8, 6> DeviceMacEqual = { 0x12, 0x10, 0x10, 0x10, 0x10, 0x11 };


// The rule runs at compile time too, so its constexpr use is checked by the compiler.
constexpr bool MacsMatch(const std::array<uint8, 12>& uid, const std::array<uint8, 6>& hostExpected, const std::array<uint8, 6>& deviceExpected) {
	std::array<uint8, 6> host{};
	std::array<uint8, 6> device{};
	DemoProducts::NetworkMacsFromUniqueId(uid.data(), host.data(), device.data());
	return host == hostExpected && device == deviceExpected;
}


static_assert(MacsMatch(UidA, HostMacA, DeviceMacA), "the MACs of the first UID");
static_assert(MacsMatch(UidEqual, HostMacEqual, DeviceMacEqual), "equal halves give different MACs");


void TestMacDerivationAtRunTime() {
	std::array<uint8, 6> host{};
	std::array<uint8, 6> device{};
	DemoProducts::NetworkMacsFromUniqueId(UidA.data(), host.data(), device.data());
	NET_EXPECT(host == HostMacA);
	NET_EXPECT(device == DeviceMacA);
	NET_EXPECT(host != device);
}


// ---- ARP ----


// Who has 192.168.7.1? Tell 192.168.7.2 (the host's own MAC): the ARP request in a broadcast frame, 42 bytes.
Bytes ArpRequestFrame(const Bytes& senderAddress, const Bytes& targetAddress) {
	return Join({
		Broadcast6, HostMac, { 0x08, 0x06 },
		{ 0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01 },
		HostMac,
		senderAddress,
		Zeros(6),
		targetAddress
	});
}


// The reply the device gives: it is at 192.168.7.1 with its own MAC, and the frame goes to the host's MAC. 42 bytes.
void TestArpRequestIsAnswered() {
	Bytes request = ArpRequestFrame(Ip192_168_7_2, Ip192_168_7_1);
	NET_EXPECT_EQ(request.size(), 42);

	Answer answer = AnswerTo(request);
	Bytes expected = Join({
		HostMac, DeviceMac, { 0x08, 0x06 },
		{ 0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x02 },
		DeviceMac,
		Ip192_168_7_1,
		HostMac,
		Ip192_168_7_2
	});
	NET_EXPECT_EQ(answer.length, 42);
	NET_EXPECT_BYTES(answer.reply, expected);
	NET_EXPECT_EQ(answer.counters.arpReplies, 1);
	NET_EXPECT_EQ(answer.counters.dropped, 0);
}


// A probe (sender address 0.0.0.0, a host checking that the address is free) is not answered.
void TestArpProbeIsNotAnswered() {
	ExpectNoAnswer(ArpRequestFrame(Ip0, Ip192_168_7_1), false);
}


// A request for another address is not answered.
void TestArpForOtherAddressIsNotAnswered() {
	ExpectNoAnswer(ArpRequestFrame(Ip192_168_7_2, Ip192_168_7_5), false);
}


// A reply (operation 2) is not answered, and a request whose sender MAC is a group address is not either.
void TestArpReplyAndGroupSenderAreNotAnswered() {
	Bytes reply = With(ArpRequestFrame(Ip192_168_7_2, Ip192_168_7_1), 20, { 0x00, 0x02 });
	ExpectNoAnswer(reply, false);

	Bytes groupSender = With(ArpRequestFrame(Ip192_168_7_2, Ip192_168_7_1), 22, { 0x01 });
	ExpectNoAnswer(groupSender, false);
}


// An ARP packet of another hardware type is malformed: the hardware type is 0x0002 here.
void TestArpMalformed() {
	Bytes wrongHardware = With(ArpRequestFrame(Ip192_168_7_2, Ip192_168_7_1), 14, { 0x00, 0x02 });
	ExpectNoAnswer(wrongHardware, true);

	Bytes truncated = ArpRequestFrame(Ip192_168_7_2, Ip192_168_7_1);
	truncated.resize(40);
	ExpectNoAnswer(truncated, true);
}


// ---- ICMP echo ----


// Echo request from 192.168.7.2 to 192.168.7.1, id 0001, sequence 0002, payload 61 62 63 64 ("abcd"). 46 bytes.
//   IPv4 header: 45 00 | total length 0020 | id 0000 | flags DF 4000 | TTL 0x80 | protocol 01 | checksum 6B89 | src | dst.
//     The header sum is 9476 without the checksum, so the checksum is ~9476 = 6B89.
//   ICMP: type 08, code 00, checksum 3336, id 0001, seq 0002, payload. The message sum is CCC9, so the checksum is ~CCC9 = 3336.
Bytes EchoRequestFrame() {
	return Join({
		DeviceMac, HostMac, { 0x08, 0x00 },
		{ 0x45, 0x00, 0x00, 0x20, 0x00, 0x00, 0x40, 0x00, 0x80, 0x01, 0x6B, 0x89 },
		Ip192_168_7_2,
		Ip192_168_7_1,
		{ 0x08, 0x00, 0x33, 0x36, 0x00, 0x01, 0x00, 0x02, 0x61, 0x62, 0x63, 0x64 }
	});
}


// The reply: the addresses and the MACs swapped, type 00, and the ICMP checksum recomputed. The reply header sum is 5476, so its
// checksum is ~5476 = AB89. The reply message sum is C4C9, so its checksum is ~C4C9 = 3B36.
Bytes EchoReplyFrame() {
	return Join({
		HostMac, DeviceMac, { 0x08, 0x00 },
		{ 0x45, 0x00, 0x00, 0x20, 0x00, 0x00, 0x40, 0x00, 0x40, 0x01, 0xAB, 0x89 },
		Ip192_168_7_1,
		Ip192_168_7_2,
		{ 0x00, 0x00, 0x3B, 0x36, 0x00, 0x01, 0x00, 0x02, 0x61, 0x62, 0x63, 0x64 }
	});
}


void TestIcmpEchoRequestIsAnswered() {
	Bytes request = EchoRequestFrame();
	NET_EXPECT_EQ(request.size(), 46);

	Answer answer = AnswerTo(request);
	NET_EXPECT_EQ(answer.length, 46);
	NET_EXPECT_BYTES(answer.reply, EchoReplyFrame());
	NET_EXPECT_EQ(answer.counters.icmpReplies, 1);
	NET_EXPECT_EQ(answer.counters.dropped, 0);
	NET_EXPECT_EQ(answer.counters.malformed, 0);
}


// A frame padded to the Ethernet minimum of 60 bytes: the IPv4 total length says where the packet ends, so the padding is not echoed.
void TestIcmpPaddingIsNotEchoed() {
	Bytes padded = EchoRequestFrame();
	padded.resize(60, 0);
	Answer answer = AnswerTo(padded);
	NET_EXPECT_EQ(answer.length, 46);
	NET_EXPECT_BYTES(answer.reply, EchoReplyFrame());
}


// The largest echo a frame allows: 1514 bytes, an IPv4 total length of 1500 and 1480 bytes of ICMP. The reply has the same size and the
// same payload, and its checksums are right. The checks use an independent sum written here, not the code under test.
Bytes ReferenceSum(const Bytes& bytes, std::size_t from, std::size_t to) {
	uint64 sum = 0;
	for (std::size_t index = from; index < to; index += 2) {
		uint64 high = bytes[index];
		uint64 low = index + 1 < to ? bytes[index + 1] : 0;
		sum += (high << 8) | low;
	}

	while ((sum >> 16) != 0) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	return Bytes{ static_cast<uint8>(sum >> 8), static_cast<uint8>(sum & 0xFF) };
}


void TestLargestEchoIsAnswered() {
	Bytes frame = Join({
		DeviceMac, HostMac, { 0x08, 0x00 },
		{ 0x45, 0x00, 0x05, 0xDC, 0x00, 0x00, 0x40, 0x00, 0x40, 0x01, 0x00, 0x00 },
		Ip192_168_7_2,
		Ip192_168_7_1
	});
	Bytes icmp(1480, 0);
	icmp[0] = 0x08;
	icmp[4] = 0x01;
	icmp[5] = 0x02;
	icmp[6] = 0x03;
	icmp[7] = 0x04;
	for (std::size_t index = 8; index < icmp.size(); index++) {
		icmp[index] = static_cast<uint8>(index * 7);
	}

	Bytes icmpSum = ReferenceSum(icmp, 0, icmp.size());
	icmp[2] = static_cast<uint8>(~icmpSum[0]);
	icmp[3] = static_cast<uint8>(~icmpSum[1]);
	frame.insert(frame.end(), icmp.begin(), icmp.end());
	Bytes header = Bytes(frame.begin() + 14, frame.begin() + 34);
	Bytes headerSum = ReferenceSum(header, 0, header.size());
	frame[24] = static_cast<uint8>(~headerSum[0]);
	frame[25] = static_cast<uint8>(~headerSum[1]);
	NET_EXPECT_EQ(frame.size(), 1514);

	Answer answer = AnswerTo(frame);
	NET_EXPECT_EQ(answer.length, 1514);
	NET_EXPECT_EQ(answer.counters.icmpReplies, 1);
	if (answer.length == 1514) {
		Bytes replyIcmp(answer.reply.begin() + 34, answer.reply.end());
		NET_EXPECT_EQ(replyIcmp[0], 0);
		NET_EXPECT(ReferenceSum(replyIcmp, 0, replyIcmp.size()) == Bytes({ 0xFF, 0xFF }));
		NET_EXPECT(ReferenceSum(answer.reply, 14, 34) == Bytes({ 0xFF, 0xFF }));
		NET_EXPECT(std::equal(replyIcmp.begin() + 8, replyIcmp.end(), icmp.begin() + 8));
	}
}


// A request with a wrong ICMP checksum (3337 for 3336) is malformed.
void TestIcmpBadChecksumIsMalformed() {
	ExpectNoAnswer(With(EchoRequestFrame(), 37, { 0x37 }), true);
}


// A request whose type is 0 (an echo reply sent to the device) is not an echo request: it is dropped, not malformed.
void TestIcmpNotAnEchoRequestIsIgnored() {
	ExpectNoAnswer(With(EchoRequestFrame(), 34, { 0x00 }), false);
}


// The same request to 192.168.7.5. The header checksum changes with the address: the sum is 9476 + 0004 = 947A, so the checksum is 6B85.
// The packet is not for the device, so it is ignored (the header is right).
void TestIcmpToOtherAddressIsIgnored() {
	Bytes frame = With(EchoRequestFrame(), 24, { 0x6B, 0x85 });
	frame[33] = 0x05;
	ExpectNoAnswer(frame, false);
}


// A fragment (more-fragments flag 2000 instead of DF 4000) is refused as malformed. The header sum is 9476 - 2000 = 7476, so the checksum is
// 8B89.
void TestIcmpFragmentIsMalformed() {
	Bytes frame = With(EchoRequestFrame(), 20, { 0x20, 0x00 });
	frame[24] = 0x8B;
	frame[25] = 0x89;
	ExpectNoAnswer(frame, true);
}


// A wrong IPv4 header checksum (6B88), a version of 6, and a total length longer than the frame are each malformed on their own. The
// total length change also breaks the checksum, which is fine: the packet is malformed either way.
void TestIpv4Malformed() {
	ExpectNoAnswer(With(EchoRequestFrame(), 25, { 0x88 }), true);
	ExpectNoAnswer(With(EchoRequestFrame(), 14, { 0x65 }), true);

	Bytes longTotal = With(EchoRequestFrame(), 16, { 0x00, 0x40 });
	ExpectNoAnswer(longTotal, true);
}


// A frame with another EtherType (IPv6, 86DD) is ignored, and a frame shorter than an Ethernet header is malformed.
void TestUnknownEtherTypeAndShortFrame() {
	ExpectNoAnswer(With(EchoRequestFrame(), 12, { 0x86, 0xDD }), false);

	Bytes shortFrame = { 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02, 0x11, 0x22, 0x33 };
	ExpectNoAnswer(shortFrame, true);
}


// ---- UDP ----


// A UDP datagram from 0.0.0.0 to 255.255.255.255, ports 68 and 67, 4 bytes of payload 01 02 03 04. The pseudo header sum is FFFF + FFFF
// + 0011 + 000C = 2001B, the datagram sum is 0499, so the checksum is ~04B6 = FB49.
void TestUdpChecksum() {
	Bytes good = { 0x00, 0x44, 0x00, 0x43, 0x00, 0x0C, 0xFB, 0x49, 0x01, 0x02, 0x03, 0x04 };
	DemoNetwork::UdpDatagram datagram;
	NET_EXPECT(DemoNetwork::ParseUdp(good, 0, 0xFFFFFFFF, datagram));
	NET_EXPECT_EQ(datagram.sourcePort, 68);
	NET_EXPECT_EQ(datagram.destinationPort, 67);
	NET_EXPECT_EQ(datagram.payload.size(), 4);

	Bytes wrong = good;
	wrong[7] = 0x48;
	NET_EXPECT(!DemoNetwork::ParseUdp(wrong, 0, 0xFFFFFFFF, datagram));

	Bytes unchecked = good;
	unchecked[6] = 0x00;
	unchecked[7] = 0x00;
	NET_EXPECT(DemoNetwork::ParseUdp(unchecked, 0, 0xFFFFFFFF, datagram));

	Bytes shortLength = good;
	shortLength[5] = 0x07;
	NET_EXPECT(!DemoNetwork::ParseUdp(shortLength, 0, 0xFFFFFFFF, datagram));
}


// ---- DHCP ----


// The client's fixed part (236 bytes): BOOTREQUEST, Ethernet, 6 bytes, transaction 12345678, the flags, ciaddr, and the host's MAC as chaddr.
Bytes ClientFixed(uint8 flagsHigh, const Bytes& ciaddr) {
	return Join({
		{ 0x01, 0x01, 0x06, 0x00 },
		{ 0x12, 0x34, 0x56, 0x78 },
		{ 0x00, 0x00 },
		{ flagsHigh, 0x00 },
		ciaddr,
		Ip0,
		Ip0,
		Ip0,
		HostMac,
		Zeros(10),
		Zeros(64),
		Zeros(128)
	});
}


// The server's fixed part (236 bytes) for an offer or an ack: the flags and ciaddr are echoed, yiaddr is the lease, siaddr the device.
Bytes ServerFixed(uint8 flagsHigh, const Bytes& ciaddr) {
	return Join({
		{ 0x02, 0x01, 0x06, 0x00 },
		{ 0x12, 0x34, 0x56, 0x78 },
		{ 0x00, 0x00 },
		{ flagsHigh, 0x00 },
		ciaddr,
		Ip192_168_7_2,
		Ip192_168_7_1,
		Ip0,
		HostMac,
		Zeros(10),
		Zeros(64),
		Zeros(128)
	});
}


// DHCP DISCOVER, broadcast flag set, from 0.0.0.0 to 255.255.255.255. The options: 53 (message type) 1, 55 (parameter list) 1, 3, 6, end.
// The message is 249 bytes. The IPv4 total length is 277 (0115) and the header checksum 39D9: the header sum is C626 without it, and the
// added words (the broadcast addresses) do not change a one's complement sum of FFFF.
Bytes DiscoverFrame() {
	return Join({
		Broadcast6, HostMac, { 0x08, 0x00 },
		{ 0x45, 0x00, 0x01, 0x15, 0x00, 0x00, 0x00, 0x00, 0x80, 0x11, 0x39, 0xD9 },
		Ip0,
		Ip255,
		{ 0x00, 0x44, 0x00, 0x43, 0x01, 0x01, 0x00, 0x00 },
		ClientFixed(0x80, Ip0),
		DhcpCookie,
		{ 0x35, 0x01, 0x01 },
		{ 0x37, 0x03, 0x01, 0x03, 0x06 },
		{ 0xFF }
	});
}


// DHCP REQUEST for 192.168.7.2 (option 50), server 192.168.7.1 (option 54): 256 bytes. IPv4 total 284 (011C), checksum 39D2 (the header sum
// is C62D without it).
Bytes RequestFrame(const Bytes& requestedAddress, const Bytes& serverAddress) {
	return Join({
		Broadcast6, HostMac, { 0x08, 0x00 },
		{ 0x45, 0x00, 0x01, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x80, 0x11, 0x39, 0xD2 },
		Ip0,
		Ip255,
		{ 0x00, 0x44, 0x00, 0x43, 0x01, 0x08, 0x00, 0x00 },
		ClientFixed(0x80, Ip0),
		DhcpCookie,
		{ 0x35, 0x01, 0x03 },
		{ 0x32, 0x04 },
		requestedAddress,
		{ 0x36, 0x04 },
		serverAddress,
		{ 0xFF }
	});
}


// The offer and the ack to a broadcast client: 342 bytes, sent to 255.255.255.255 with the broadcast MAC. The IPv4 total length is 328
// (0148) and the header checksum is 71FC (the header sum is 8E03). The UDP checksum is not compared here; ParseUdp checks it.
Bytes BroadcastAnswerFrame(uint8 messageType) {
	return Join({
		Broadcast6, DeviceMac, { 0x08, 0x00 },
		{ 0x45, 0x00, 0x01, 0x48, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0x71, 0xFC },
		Ip192_168_7_1,
		Ip255,
		{ 0x00, 0x43, 0x00, 0x44, 0x01, 0x34, 0x00, 0x00 },
		ServerFixed(0x80, Ip0),
		DhcpCookie,
		{ 0x35, 0x01, messageType },
		{ 0x36, 0x04, 0xC0, 0xA8, 0x07, 0x01 },
		{ 0x33, 0x04, 0x00, 0x00, 0x0E, 0x10 },
		{ 0x01, 0x04, 0xFF, 0xFF, 0xFF, 0x00 },
		{ 0xFF },
		Zeros(38)
	});
}


// The UDP checksum of a reply is not one of the hand-written bytes: the reply's checksum must verify with the pseudo header.
void ExpectReplyUdpVerifies(const Bytes& reply, uint32 source, uint32 destination) {
	DemoNetwork::UdpDatagram datagram;
	std::span<const uint8> udp = std::span<const uint8>(reply).subspan(34, 308);
	NET_EXPECT(DemoNetwork::ParseUdp(udp, source, destination, datagram));
	NET_EXPECT_EQ(datagram.payload.size(), 300);
}


void TestDhcpDiscoverGetsOffer() {
	Bytes discover = DiscoverFrame();
	NET_EXPECT_EQ(discover.size(), 291);

	Answer answer = AnswerTo(discover);
	NET_EXPECT_EQ(answer.length, 342);
	NET_EXPECT_EQ(answer.counters.dhcpOffers, 1);
	NET_EXPECT_EQ(answer.counters.dropped, 0);
	if (answer.length == 342) {
		NET_EXPECT_BYTES_EXCEPT(answer.reply, BroadcastAnswerFrame(2), 40, 42);
		ExpectReplyUdpVerifies(answer.reply, 0xC0A80701, 0xFFFFFFFF);
	}
}


void TestDhcpRequestGetsAck() {
	Bytes request = RequestFrame(Ip192_168_7_2, Ip192_168_7_1);
	NET_EXPECT_EQ(request.size(), 298);

	Answer answer = AnswerTo(request);
	NET_EXPECT_EQ(answer.length, 342);
	NET_EXPECT_EQ(answer.counters.dhcpAcks, 1);
	NET_EXPECT_EQ(answer.counters.dropped, 0);
	if (answer.length == 342) {
		NET_EXPECT_BYTES_EXCEPT(answer.reply, BroadcastAnswerFrame(5), 40, 42);
		ExpectReplyUdpVerifies(answer.reply, 0xC0A80701, 0xFFFFFFFF);
	}
}


// A renewal: the client has 192.168.7.2 in ciaddr (no options 50 or 54), sends to the device unicast, and asks for no broadcast. The ack
// goes to the client's address with its MAC: destination 02 11 22 33 44 55 and 192.168.7.2. The IPv4 total length is 272 (0110), the checksum
// AA89 (the header sum is 5576), and the ack's header sum is 55AE, so its checksum is AA51.
void TestDhcpRenewalGetsUnicastAck() {
	Bytes renew = Join({
		DeviceMac, HostMac, { 0x08, 0x00 },
		{ 0x45, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x80, 0x11, 0xAA, 0x89 },
		Ip192_168_7_2,
		Ip192_168_7_1,
		{ 0x00, 0x44, 0x00, 0x43, 0x00, 0xFC, 0x00, 0x00 },
		ClientFixed(0x00, Ip192_168_7_2),
		DhcpCookie,
		{ 0x35, 0x01, 0x03 },
		{ 0xFF }
	});
	NET_EXPECT_EQ(renew.size(), 286);

	Answer answer = AnswerTo(renew);
	Bytes expected = Join({
		HostMac, DeviceMac, { 0x08, 0x00 },
		{ 0x45, 0x00, 0x01, 0x48, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0xAA, 0x51 },
		Ip192_168_7_1,
		Ip192_168_7_2,
		{ 0x00, 0x43, 0x00, 0x44, 0x01, 0x34, 0x00, 0x00 },
		ServerFixed(0x00, Ip192_168_7_2),
		DhcpCookie,
		{ 0x35, 0x01, 0x05 },
		{ 0x36, 0x04, 0xC0, 0xA8, 0x07, 0x01 },
		{ 0x33, 0x04, 0x00, 0x00, 0x0E, 0x10 },
		{ 0x01, 0x04, 0xFF, 0xFF, 0xFF, 0x00 },
		{ 0xFF },
		Zeros(38)
	});
	NET_EXPECT_EQ(answer.length, 342);
	NET_EXPECT_EQ(answer.counters.dhcpAcks, 1);
	if (answer.length == 342) {
		NET_EXPECT_BYTES_EXCEPT(answer.reply, expected, 40, 42);
		ExpectReplyUdpVerifies(answer.reply, 0xC0A80701, 0xC0A80702);
	}
}


// A REQUEST for another address (192.168.7.9) and one that names another server (192.168.7.50) get no answer.
void TestDhcpRequestForOtherAddressOrServerIsIgnored() {
	ExpectNoAnswer(RequestFrame(Ip192_168_7_9, Ip192_168_7_1), false);
	ExpectNoAnswer(RequestFrame(Ip192_168_7_2, Ip192_168_7_50), false);
}


// A message of another type (INFORM, 8) gets no answer.
void TestDhcpOtherMessageTypeIsIgnored() {
	Bytes frame = With(DiscoverFrame(), 42 + 242, { 0x08 });
	ExpectNoAnswer(frame, false);
}


// Malformed DHCP: a bad op (2 in a client message), a bad magic cookie, a message type option of 2 bytes, an option that runs past the
// message, and a message with no end option. The UDP checksum of these frames is 0, so the only broken rule is the one named.
void TestDhcpMalformed() {
	ExpectNoAnswer(With(DiscoverFrame(), 42, { 0x02 }), true);
	ExpectNoAnswer(With(DiscoverFrame(), 42 + 236, { 0x00 }), true);
	ExpectNoAnswer(With(DiscoverFrame(), 42 + 240 + 1, { 0x02 }), true);
	ExpectNoAnswer(With(DiscoverFrame(), 42 + 243 + 1, { 0x30 }), true);
	ExpectNoAnswer(With(DiscoverFrame(), 42 + 248, { 0x00 }), true);
}


// A datagram to another port (69) is ignored: its checksum is right (0), so only the port decides.
void TestUdpToOtherPortIsIgnored() {
	ExpectNoAnswer(With(DiscoverFrame(), 36, { 0x00, 0x45 }), false);
}


// A DHCP message of 100 bytes is shorter than the 244 the parser takes at least, so it is malformed. The packet itself is whole: the IPv4
// total length is 128 (0080), the UDP length 108 (006C), and the header checksum 3A6E (the header sum is C591 without it).
void TestDhcpTooShortIsMalformed() {
	Bytes dhcp = ClientFixed(0x80, Ip0);
	dhcp.resize(100);
	Bytes frame = Join({
		Broadcast6, HostMac, { 0x08, 0x00 },
		{ 0x45, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x11, 0x3A, 0x6E },
		Ip0,
		Ip255,
		{ 0x00, 0x44, 0x00, 0x43, 0x00, 0x6C, 0x00, 0x00 },
		dhcp
	});
	NET_EXPECT_EQ(frame.size(), 14 + 128);
	ExpectNoAnswer(frame, true);
}


// The DISCOVER and REQUEST frames the tests write are checked here: their IPv4 headers, with the checksums written by hand, must verify
// with ParseIpv4, so a wrong hand-written checksum would show up as a failure here rather than as a wrong answer later.
void TestHandWrittenDhcpHeadersVerify() {
	DemoNetwork::Ipv4Packet discover;
	Bytes discoverFrame = DiscoverFrame();
	NET_EXPECT(DemoNetwork::ParseIpv4(std::span<const uint8>(discoverFrame).subspan(14), discover));
	NET_EXPECT_EQ(discover.payload.size(), 257);

	DemoNetwork::Ipv4Packet request;
	Bytes requestFrame = RequestFrame(Ip192_168_7_2, Ip192_168_7_1);
	NET_EXPECT(DemoNetwork::ParseIpv4(std::span<const uint8>(requestFrame).subspan(14), request));
	NET_EXPECT_EQ(request.payload.size(), 264);
}


// ---- ARP and IP helpers ----


// The address predicates: the device never answers to 0.0.0.0, the broadcast address or a multicast group, and a leased address is unicast.
void TestUnicastAddressRule() {
	NET_EXPECT(!DemoNetwork::IsUnicastAddress(0));
	NET_EXPECT(!DemoNetwork::IsUnicastAddress(0xFFFFFFFF));
	NET_EXPECT(!DemoNetwork::IsUnicastAddress(0xE0000001));
	NET_EXPECT(DemoNetwork::IsUnicastAddress(0xC0A80702));
}


}


int main() {
	TestChecksumRfcExample();
	TestChecksumOddLength();
	TestChecksumInitialValue();
	TestMacDerivationAtRunTime();
	TestArpRequestIsAnswered();
	TestArpProbeIsNotAnswered();
	TestArpForOtherAddressIsNotAnswered();
	TestArpReplyAndGroupSenderAreNotAnswered();
	TestArpMalformed();
	TestIcmpEchoRequestIsAnswered();
	TestIcmpPaddingIsNotEchoed();
	TestLargestEchoIsAnswered();
	TestIcmpBadChecksumIsMalformed();
	TestIcmpNotAnEchoRequestIsIgnored();
	TestIcmpToOtherAddressIsIgnored();
	TestIcmpFragmentIsMalformed();
	TestIpv4Malformed();
	TestUnknownEtherTypeAndShortFrame();
	TestUdpChecksum();
	TestDhcpDiscoverGetsOffer();
	TestDhcpRequestGetsAck();
	TestDhcpRenewalGetsUnicastAck();
	TestDhcpRequestForOtherAddressOrServerIsIgnored();
	TestDhcpOtherMessageTypeIsIgnored();
	TestDhcpMalformed();
	TestUdpToOtherPortIsIgnored();
	TestDhcpTooShortIsMalformed();
	TestHandWrittenDhcpHeadersVerify();
	TestUnicastAddressRule();
	return SimTest::Finish("TestNetworkDemo");
}
