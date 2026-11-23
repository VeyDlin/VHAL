#include <VHAL.h>
#include "CdcTestDevices.h"
#include "HidTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/HID/HidFunction.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Link/BulkLink.h>
#include <Drivers/Interface/VUB/Link/CdcLink.h>
#include <Drivers/Interface/VUB/Link/HidLink.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// The three links over the real USB functions: CdcAcmFunction, VendorBulkFunction and HidFunction, on the simulated controller of the
// USB module. The device side runs a stack with COBS over each link, and the host side is the simulated host: it writes and reads the
// endpoints as a PC does, and checks the bytes of the frames by hand.


// The payloads the device stacks handed up, for one stack.
struct Inbox {
	std::array<uint8, 64> bytes{};
	std::size_t length = 0;
	std::size_t count = 0;
};


inline Inbox cdcInbox;
inline Inbox bulkInbox;
inline Inbox hidInbox;


inline void StoreInto(Inbox& inbox, std::span<const uint8> payload) {
	inbox.count++;
	inbox.length = payload.size();
	for (std::size_t index = 0; index < payload.size() && index < inbox.bytes.size(); index++) {
		inbox.bytes[index] = payload[index];
	}
}


inline void OnCdcPayload(const VUB::Meta&, std::span<const uint8> payload) {
	StoreInto(cdcInbox, payload);
}


inline void OnBulkPayload(const VUB::Meta&, std::span<const uint8> payload) {
	StoreInto(bulkInbox, payload);
}


inline void OnHidPayload(const VUB::Meta&, std::span<const uint8> payload) {
	StoreInto(hidInbox, payload);
}


// The receiver of the HID link: a packet goes into hidInbox through the stack's receive path, but the link's own receiver is also
// given here, so a test can see what the link fed.
inline void FeedHidPacket(void* owner, std::span<const uint8> packet) {
	Inbox* inbox = static_cast<Inbox*>(owner);
	StoreInto(*inbox, packet);
}


inline void FeedNothing(void*, std::span<const uint8>) {
}


inline std::size_t NoRoom(void*) {
	return 0;
}


using CdcLinkType = VUB::CdcLink<CdcTest::Function>;
using BulkLinkType = VUB::BulkLink<VendorBulkFunction<512, 256, 2>>;
using HidLinkType = VUB::HidLink<HidFunction<8>, 8>;


// The traits and the concepts of the real links, with the real functions as their types.
static_assert(VUB::StreamLink<CdcLinkType>);
static_assert(VUB::ReportingLink<CdcLinkType>);
static_assert(VUB::PollingLink<CdcLinkType>);
static_assert(VUB::StreamLink<BulkLinkType>);
static_assert(VUB::ReportingLink<BulkLinkType>);
static_assert(VUB::PollingLink<BulkLinkType>);
static_assert(VUB::PacketLink<HidLinkType>);
static_assert(VUB::ReceivingLink<HidLinkType>);
static_assert(HidLinkType::traits.maxPacket == 7);


using CdcDeviceStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<CdcLinkType>()
		.MaxPayload(16)
		.OnReceive<&OnCdcPayload>();
}>;


using BulkDeviceStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<BulkLinkType>()
		.MaxPayload(16)
		.OnReceive<&OnBulkPayload>();
}>;


// Before configuration the link is down and refuses writes. After the host sets DTR and enumerates, the stack is up. The host writes a
// COBS frame of {33 00 44}, and the device stack gives the payload up. The device sends {11 00 22}, and the host reads the frame
// 02 11 02 22 00 from the bulk IN endpoint.
void TestCdcLinkOverTheRealFunction() {
	cdcInbox = Inbox();
	CdcTest::CdcFixture fixture;
	CdcLinkType link(fixture.function);
	CdcDeviceStack stack(link);
	SIM_EXPECT(!stack.LinkUp());
	std::array<uint8, 2> probe = {1, 2};
	SIM_EXPECT_EQ(link.Write(probe).Value(), 0);

	fixture.Configure();
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());

	SIM_EXPECT_EQ(fixture.SetControlLines(0x0003), ResultStatus::ok);
	stack.Process();
	SIM_EXPECT(stack.LinkUp());

	std::array<uint8, 5> frame = {0x02, 0x33, 0x02, 0x44, 0x00};
	SIM_EXPECT(fixture.sim.HostOut(0x02, frame).IsOk());
	stack.Process();
	SIM_EXPECT_EQ(cdcInbox.count, 1);
	SIM_EXPECT_EQ(cdcInbox.length, 3);
	SIM_EXPECT_EQ(cdcInbox.bytes[0], 0x33);
	SIM_EXPECT_EQ(cdcInbox.bytes[1], 0x00);
	SIM_EXPECT_EQ(cdcInbox.bytes[2], 0x44);

	std::array<uint8, 3> reply = {0x11, 0x00, 0x22};
	SIM_EXPECT(stack.Send(reply) == ResultStatus::ok);
	stack.Process();
	fixture.device.Process();

	std::array<uint8, 64> in{};
	uint32 received = 0;
	SIM_EXPECT(fixture.sim.HostIn(0x81, in, received) == ResultStatus::ok);
	SIM_EXPECT_EQ(received, 5);
	SIM_EXPECT_EQ(in[0], 0x02);
	SIM_EXPECT_EQ(in[1], 0x11);
	SIM_EXPECT_EQ(in[2], 0x02);
	SIM_EXPECT_EQ(in[3], 0x22);
	SIM_EXPECT_EQ(in[4], 0x00);
}


// The same over the vendor bulk function. A vendor function has no DTR: it is up as soon as it is configured.
void TestBulkLinkOverTheRealFunction() {
	bulkInbox = Inbox();
	CdcTest::VendorFixtureOf<CdcTest::VendorUsb> fixture;
	BulkLinkType link(fixture.function);
	BulkDeviceStack stack(link);
	fixture.Configure();
	stack.Process();
	SIM_EXPECT(stack.LinkUp());

	std::array<uint8, 5> frame = {0x02, 0x33, 0x02, 0x44, 0x00};
	SIM_EXPECT(fixture.sim.HostOut(0x02, frame).IsOk());
	stack.Process();
	SIM_EXPECT_EQ(bulkInbox.count, 1);
	SIM_EXPECT_EQ(bulkInbox.length, 3);
	SIM_EXPECT_EQ(bulkInbox.bytes[0], 0x33);

	std::array<uint8, 3> reply = {0x11, 0x00, 0x22};
	SIM_EXPECT(stack.Send(reply) == ResultStatus::ok);
	stack.Process();
	fixture.device.Process();

	std::array<uint8, 64> in{};
	uint32 received = 0;
	SIM_EXPECT(fixture.sim.HostIn(0x81, in, received) == ResultStatus::ok);
	SIM_EXPECT_EQ(received, 5);
	SIM_EXPECT_EQ(in[1], 0x11);
	SIM_EXPECT_EQ(in[3], 0x22);
}


// The HID link over the function with 8-byte reports (the vendor report of the HID test devices). Before configuration Send is busy.
// Once configured, a packet of three bytes goes out as the report 03 01 02 03 00 00 00 00, and an output report from the host with the
// payload AA BB reaches the receiver as a packet.
void TestHidLinkOverTheRealFunction() {
	hidInbox = Inbox();
	HidTest::HidDevice<HidTest::VendorUsb, HidTest::VendorReport, 8> hid;
	HidLinkType link(hid.function);
	VUB::Receiver receiver(&hidInbox, FeedNothing, FeedHidPacket, NoRoom);
	link.SetReceiver(receiver);
	SIM_EXPECT(link.ReceiveStatus() == ResultStatus::ok);

	std::array<uint8, 3> packet = {1, 2, 3};
	SIM_EXPECT(link.Send(packet) == ResultStatus::busy);

	hid.Configure();
	SIM_EXPECT(link.Send(packet) == ResultStatus::ok);
	hid.device.Process();

	std::array<uint8, 8> in{};
	uint32 received = 0;
	SIM_EXPECT(hid.sim.HostIn(0x81, in, received) == ResultStatus::ok);
	SIM_EXPECT_EQ(received, 8);
	SIM_EXPECT_EQ(in[0], 3);
	SIM_EXPECT_EQ(in[1], 1);
	SIM_EXPECT_EQ(in[2], 2);
	SIM_EXPECT_EQ(in[3], 3);
	SIM_EXPECT_EQ(in[4], 0);

	std::array<uint8, 8> out = {2, 0xAA, 0xBB, 0, 0, 0, 0, 0};
	SIM_EXPECT(hid.sim.HostOut(0x02, out).IsOk());
	SIM_EXPECT_EQ(hidInbox.count, 1);
	SIM_EXPECT_EQ(hidInbox.length, 2);
	SIM_EXPECT_EQ(hidInbox.bytes[0], 0xAA);
	SIM_EXPECT_EQ(hidInbox.bytes[1], 0xBB);
}


int main() {
	TestCdcLinkOverTheRealFunction();
	TestBulkLinkOverTheRealFunction();
	TestHidLinkOverTheRealFunction();
	return SimTest::Finish("TestVubLinkUsb");
}
