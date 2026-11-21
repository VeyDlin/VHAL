#include <DemoUSBNetwork/DemoUSBNetwork.h>
#include <DemoUSBNetwork/Descriptors/NetworkDescriptors.h>
#include <DemoUSBNetwork/Network/EthernetFrame.h>
#include <DemoUSBNetwork/Network/NetworkResponder.h>
#include <DemoProducts.h>
#include <Drivers/Interface/USB/Class/CDC/NcmFunction.h>
#include <Drivers/Interface/USB/Descriptor/USBHexStringDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBSerialNumber.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <cstddef>
#include <span>


// The result of BSP::Init() for USART1, readable over SWD. ok means the console is working.
volatile ResultStatus networkConsoleStatus __attribute__((used)) = ResultStatus::ok;


namespace DemoUSBNetwork {
constexpr uint64 StatusIntervalMs = 1000;


// The NCM function: NTBs of 2048 bytes each way (the minimum the spec allows), three frames queued for the host and two received frames
// waiting for the responder. Each queued frame costs 1520 bytes of RAM, so the depths are the largest that keep the image within 20 KB
// (the README has the figures). A burst of replies beyond the queue is refused and counted as txRefused.
using NetworkNcm = NcmFunction<2048, 2048, 3, 2>;


static NetworkNcm ncmFunction(NetworkUsb::Function(0));
static std::array<USBFunction*, 1> functions = { &ncmFunction };
static USBDeviceAdapter::Parameters usbParameters;
static USBDevice usbDevice(BSP::usbAdapter, NetworkUsb::Set, functions);


// The serial number and the MAC string are kept in static storage, because the device keeps their spans until it is stopped.
static std::array<uint8, 2 + 4 * BSP::UniqueIdSize> serialDescriptor{};
static std::array<uint8, 2 + 4 * 6> macDescriptor{};


// The device's identity on the link, and the counters of the responder and of the frames it could not queue.
static DemoNetwork::NetworkIdentity identity;
static DemoNetwork::NetworkCounters counters;
static uint32 txRefused = 0;


// One frame in each direction. The responder reads rxFrame and writes replyFrame; the NCM function copies both, so the buffers are reused
// on every frame.
static std::array<uint8, DemoNetwork::EthernetMaxFrameSize> rxFrame{};
static std::array<uint8, DemoNetwork::EthernetMaxFrameSize> replyFrame{};


// Reads the unique ID once and derives from it the serial number, the host's MAC (the string the host shows for its own adapter) and the
// device's MAC. The same bytes give the same values after a reboot.
static ResultStatus SetIdentity() {
	std::array<uint8, BSP::UniqueIdSize> uniqueId{};
	BSP::ReadUniqueId(uniqueId);

	ResultStatus status = MakeUSBSerialNumberDescriptor(uniqueId, serialDescriptor);
	if (status != ResultStatus::ok) {
		return status;
	}

	status = usbDevice.SetSerialNumber(serialDescriptor);
	if (status != ResultStatus::ok) {
		return status;
	}

	std::array<uint8, 6> hostMac{};
	std::array<uint8, 6> deviceMac{};
	DemoProducts::NetworkMacsFromUniqueId(uniqueId.data(), hostMac.data(), deviceMac.data());
	status = MakeUSBHexStringDescriptor(hostMac, macDescriptor);
	if (status != ResultStatus::ok) {
		return status;
	}

	status = usbDevice.SetRuntimeString(ncmFunction.MacStringIndex(), macDescriptor);
	if (status != ResultStatus::ok) {
		return status;
	}

	identity.mac = deviceMac;
	identity.address = DemoProducts::NetworkDeviceAddress;
	identity.leasedAddress = DemoProducts::NetworkLeasedAddress;
	return ResultStatus::ok;
}


// Takes every frame the host has sent, answers it when it is one the device answers, and queues the answer. The loop ends when the
// receive queue is empty; the NCM function refills it only in Process(), so the loop cannot run on without end.
static void PumpNetwork() {
	while (ncmFunction.NextFrameLength() != 0) {
		uint16 length = ncmFunction.ReceiveFrame(rxFrame);
		if (length == 0) {
			break;
		}

		std::size_t replyLength = DemoNetwork::AnswerFrame(std::span<const uint8>(rxFrame).first(length), replyFrame, identity, counters);
		if (replyLength == 0) {
			continue;
		}

		if (ncmFunction.SendFrame(std::span<const uint8>(replyFrame).first(replyLength)) != ResultStatus::ok) {
			txRefused++;
		}
	}
}


static void PrintStatus() {
	const char* stateName = USBDeviceStateName(usbDevice.GetState());

	const char* dataName = "idle";
	if (ncmFunction.IsDataPathActive()) {
		dataName = "active";
	}

	const char* linkName = "down";
	if (ncmFunction.IsLinkUp()) {
		linkName = "up";
	}

	NetworkNcm::Counters link = ncmFunction.GetCounters();

	// The line goes to the console ring, which never waits for USART1. The frame counts are the NCM function's: frames received in NTBs
	// and frames packed into NTBs for the host. dropped counts the frames the device had no answer for.
	System::console
		<< "t=" << System::GetMs()
		<< "ms usb=" << stateName
		<< " data=" << dataName
		<< " link=" << linkName
		<< " in=" << link.framesReceived
		<< " out=" << link.framesSent
		<< " arp=" << counters.arpReplies
		<< " icmp=" << counters.icmpReplies
		<< " dhcpOffer=" << counters.dhcpOffers
		<< " dhcpAck=" << counters.dhcpAcks
		<< " dropped=" << counters.dropped
		<< " malformed=" << counters.malformed
		<< " rxDropped=" << link.framesDroppedReceive
		<< " txRefused=" << txRefused
		<< " usbErr=" << usbDevice.GetErrorCount()
		<< Console::endl;
}


void Run(ResultStatus consoleStatus) {
	networkConsoleStatus = consoleStatus;

	if (SetIdentity() != ResultStatus::ok) {
		System::console << "USB network identity failed" << Console::endl;
	}

	ResultStatus status = usbDevice.Start(usbParameters);
	if (status == ResultStatus::ok) {
		System::console << "USB network started" << Console::endl;
	} else {
		System::console << "USB start failed" << Console::endl;
	}

	uint64 nextStatusMs = System::GetMs() + StatusIntervalMs;
	while (true) {
		usbDevice.Process();
		PumpNetwork();

		if (System::GetMs() >= nextStatusMs) {
			nextStatusMs += StatusIntervalMs;
			PrintStatus();
		}
	}
}
}
