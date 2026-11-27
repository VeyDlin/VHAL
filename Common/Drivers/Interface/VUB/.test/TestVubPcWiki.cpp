#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/PC/BulkLink.h>
#include <Drivers/Interface/VUB/PC/HidLink.h>
#include <Drivers/Interface/VUB/PC/SerialLink.h>
#include <Drivers/Interface/VUB/PC/TcpLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>


// The snippets of the PC wiki pages, as they are written there. They are compiled and never run: no test opens a device, a port or a
// socket of the machine. The libraries are linked so that the opening code can be linked as well.


// --- snippet: SerialLink ---
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/PC/SerialLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>

// The receive handler: each COBS frame that arrives is one payload.
void OnPortPayload(const VUB::Meta&, std::span<const uint8>) {
}

// The clock, in microseconds. The COBS framing does not read it, so a stack without a timed block could leave it out.
inline uint32 PortClockUs() {
	return 0;
}

using PortStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::PC::SerialLink>()
		.MaxPayload(64)
		.ReceiveRing(256)
		.OnReceive<&OnPortPayload>()
		.Clock<&PortClockUs>();
}>;

// Opens COM11 at 115200 8N1 and runs the stack once. The application calls Process in its loop.
void RunPort() {
	VUB::PC::SerialLink port;
	PortStack stack(port);
	VUB::PC::SerialLink::Settings settings;
	if (port.Open("COM11", settings) == ResultStatus::ok) {
		stack.Process();
	}
}
// --- end snippet ---


// --- snippet: TcpLink ---
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/PC/TcpLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>

void OnNetPayload(const VUB::Meta&, std::span<const uint8>) {
}

inline uint32 NetClockUs() {
	return 0;
}

using NetStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::PC::TcpLink>()
		.MaxPayload(64)
		.ReceiveRing(256)
		.OnReceive<&OnNetPayload>()
		.Clock<&NetClockUs>();
}>;

// The listening side accepts one peer in Process. Port 0 would let the system pick; BoundPort reads it back.
void RunListener() {
	VUB::PC::TcpLink link;
	NetStack stack(link);
	if (link.Listen("127.0.0.1", 5000) == ResultStatus::ok) {
		stack.Process();
	}
}

// The client connects; the connection completes in Process, and the link is up from then.
void RunClient() {
	VUB::PC::TcpLink link;
	NetStack stack(link);
	if (link.Connect("127.0.0.1", 5000) == ResultStatus::ok) {
		stack.Process();
	}
}
// --- end snippet ---


// --- snippet: BulkLink ---
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/PC/BulkLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>

void OnBulkPayload(const VUB::Meta&, std::span<const uint8>) {
}

inline uint32 BulkClockUs() {
	return 0;
}

using BulkStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::PC::BulkLink>()
		.MaxPayload(64)
		.ReceiveRing(256)
		.OnReceive<&OnBulkPayload>()
		.Clock<&BulkClockUs>();
}>;

// Opens the vendor interface: on Windows by its interface GUID, on Linux by the vendor and product IDs and the interface number.
void RunBulk() {
	VUB::PC::BulkLink::Settings settings;
	settings.interfaceGuid = "{53B72DEF-1477-422A-B645-D5267E3E44A6}";
	settings.vendorId = 0x1209;
	settings.productId = 0x0007;
	settings.interfaceNumber = 4;
	VUB::PC::BulkLink link;
	BulkStack stack(link);
	if (link.Open(settings) == ResultStatus::ok) {
		stack.Process();
	}
}
// --- end snippet ---


// --- snippet: HidLink ---
#include <Drivers/Interface/VUB/PC/HidLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>

void OnReportPayload(const VUB::Meta&, std::span<const uint8>) {
}

inline uint32 ReportClockUs() {
	return 0;
}

// A packet link: there is no framing, and a 64-byte report carries 63 bytes of packet.
using ReportStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<VUB::PC::HidLink<64>>()
		.MaxPayload(60)
		.ReceiveQueue(4)
		.OnReceive<&OnReportPayload>()
		.Clock<&ReportClockUs>();
}>;

// Opens the first device with the vendor ID, product ID and the vendor usage page 0xFF00.
void RunHid() {
	VUB::PC::HidLink<64> link;
	ReportStack stack(link);
	if (link.Open(0x1209, 0x0007, 0xFF00) == ResultStatus::ok) {
		stack.Process();
	}
}
// --- end snippet ---


int main() {
	return SimFinish("TestVubPcWiki");
}
