#include "SimCheck.h"
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Link/HidReport.h>
#include <Drivers/Interface/VUB/PC/HidLink.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>


// The HID packet link is compiled on both systems. Its report layout is checked here as plain functions: the output report has the ID 0
// in front, then the device's report (a length byte, the payload, zero padding). No test opens a device.


using Hid = VUB::PC::HidLink<64>;
using SmallHid = VUB::PC::HidLink<16>;


static_assert(VUB::PacketLink<Hid>, "the HID link is a packet link: Send, no stream Write");
static_assert(!VUB::StreamLink<Hid>, "the HID link is not a byte stream");
static_assert(VUB::PollingLink<Hid>, "the HID link is polled from Process");
static_assert(VUB::ReportingLink<Hid>, "the HID link reports its state");
static_assert(Hid::traits.framed, "a packet link is framed");
static_assert(Hid::traits.maxPacket == 63, "a 64-byte report carries 63 bytes of packet");
static_assert(SmallHid::traits.maxPacket == 15, "a 16-byte report carries 15 bytes of packet");


// The output report of a packet: the ID 0, the length byte, the payload and zero padding up to the report size.
inline void CheckOutputLayout() {
	const std::array<uint8, 3> payload = {1, 2, 3};
	std::array<uint8, 65> output{};
	Result<std::size_t> packed = Hid::PackOutput(payload, output);
	SIM_EXPECT(packed.IsOk());
	SIM_EXPECT(packed.Value() == 65);
	SIM_EXPECT(output[0] == 0);
	SIM_EXPECT(output[1] == 3);
	SIM_EXPECT(output[2] == 1 && output[3] == 2 && output[4] == 3);
	SIM_EXPECT(output[5] == 0 && output[64] == 0);

	// The device's report is the output without its ID byte, and reads back to the payload.
	Result<std::span<const uint8>> received = VUB::HidReportPayload(std::span<const uint8>(output).subspan(1));
	SIM_EXPECT(received.IsOk());
	SIM_EXPECT(received.Value().size() == 3);
	SIM_EXPECT(received.Value()[0] == 1 && received.Value()[2] == 3);
}


// A packet that does not fit the report, an empty one, or an output of the wrong size is refused.
inline void CheckRefusedPackets() {
	std::array<uint8, 65> output{};
	const std::array<uint8, 64> tooLong{};
	SIM_EXPECT(Hid::PackOutput(tooLong, output).Error() == ResultStatus::invalidParameter);
	SIM_EXPECT(Hid::PackOutput(std::span<const uint8>(), output).Error() == ResultStatus::invalidParameter);

	std::array<uint8, 64> wrongSize{};
	const std::array<uint8, 3> payload = {1, 2, 3};
	SIM_EXPECT(Hid::PackOutput(payload, wrongSize).Error() == ResultStatus::invalidParameter);

	// The largest packet of the 64-byte report is 63 bytes.
	std::array<uint8, 63> largest{};
	SIM_EXPECT(Hid::PackOutput(largest, output).IsOk());
}


// The report size is a parameter: a 16-byte report has a 17-byte output with the ID in front.
inline void CheckOtherReportSize() {
	const std::array<uint8, 2> payload = {7, 8};
	std::array<uint8, 17> output{};
	SIM_EXPECT(SmallHid::PackOutput(payload, output).Value() == 17);
	SIM_EXPECT(output[0] == 0 && output[1] == 2 && output[2] == 7 && output[3] == 8);
}


// The usage page of a report descriptor is its first Usage Page item.
inline void CheckUsagePage() {
	const std::array<uint8, 4> generic = {0x05, 0x01, 0x09, 0x06};
	SIM_EXPECT(Hid::UsagePageOf(generic) == 0x01);

	const std::array<uint8, 5> vendor = {0x06, 0xA0, 0xFF, 0x09, 0x01};
	SIM_EXPECT(Hid::UsagePageOf(vendor) == 0xFFA0);

	const std::array<uint8, 4> consumer = {0x09, 0x01, 0x05, 0x0C};
	SIM_EXPECT(Hid::UsagePageOf(consumer) == 0x0C);

	const std::array<uint8, 2> noPage = {0x09, 0x06};
	SIM_EXPECT(Hid::UsagePageOf(noPage) == 0);

	const std::array<uint8, 2> truncated = {0x06, 0xA0};
	SIM_EXPECT(Hid::UsagePageOf(truncated) == 0);
}


// A link that is not open refuses to send and polls nothing.
inline void CheckClosedLink() {
	Hid link;
	SIM_EXPECT(!link.IsOpen());
	const std::array<uint8, 2> payload = {1, 2};
	SIM_EXPECT(link.Send(payload) == ResultStatus::noConnection);
	link.Poll(VUB::Receiver());
	SIM_EXPECT(!link.IsOpen());
}


#if defined(_WIN32)
// The detail buffer is used only when the size query failed for lack of a buffer, and the size it gave holds the fixed part of the
// detail structure, so the buffer can take the cbSize field.
inline void CheckDetailSize() {
	const DWORD fixedPart = static_cast<DWORD>(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath));
	SIM_EXPECT(!Hid::DetailSizeUsable(true, 0));
	SIM_EXPECT(!Hid::DetailSizeUsable(true, fixedPart - 1));
	SIM_EXPECT(Hid::DetailSizeUsable(true, fixedPart));
	SIM_EXPECT(Hid::DetailSizeUsable(true, static_cast<DWORD>(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))));
	SIM_EXPECT(!Hid::DetailSizeUsable(false, static_cast<DWORD>(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))));
}
#endif


int main() {
	CheckOutputLayout();
	CheckRefusedPackets();
	CheckOtherReportSize();
	CheckUsagePage();
	CheckClosedLink();
#if defined(_WIN32)
	CheckDetailSize();
#endif
	return SimFinish("TestVubPcHid");
}
