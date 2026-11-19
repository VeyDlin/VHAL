#include <VHAL.h>
#include "FixedList.h"
#include "SimAssert.h"
#include "SimFsdevController.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>


namespace {


// USBFsdev<Family> on the register model of SimFsdevController.h: the port's own register code runs, and the controller
// answers each EPnR access with the rules of RM0440. The host is scripted per token. The cases are the multi-packet
// transfers on double-buffered and single-buffered endpoints, a CLEAR_FEATURE(ENDPOINT_HALT) in the middle of a transfer
// (with and without a completed packet still pending), a token that arrives between the take-back and the re-arm of a
// ClearStall(), a single-buffered endpoint reopened double-buffered after an unserved completion, and Stall() during a
// buffer conflict. The later cases count the tokens NAKed while the CPU serves the interrupts late, and check a double-buffered
// OUT packet held across a transfer boundary, the zero-length packet, Stall, CLEAR_FEATURE, close and bus reset with both
// buffers in use.


constexpr uint32 MaxPacketSize = 64;
constexpr uint32 TransferSize = 200;
// Host polls before a helper gives up. A transfer of TransferSize bytes takes about four packets, so this is far more than a
// working endpoint needs; a stuck endpoint fails the check after the helper instead of hanging the test.
constexpr uint32 MaxPolls = 256;


class TestFsdevPort : public USBFsdev<SimFsdevFamily> {
public:
	struct Completion {
		uint8 address = 0;
		uint32 byteCount = 0;
	};


	FixedList<Completion, 16> completions;


	TestFsdevPort()
		: USBFsdev(SimFsdevController::RegisterBase) {
	}


protected:
	void CallTransferComplete(uint8 address, uint32 byteCount) override {
		completions.Push(Completion{address, byteCount});
	}
};


// The host side of one device. It ACKs every packet it gets (USB 2.0 section 8.6.4), and keeps the data of a packet only when
// the PID is the one it expects. The expected PID alternates on each kept packet. A packet with another PID is dropped and
// counted. The host sends CLEAR_FEATURE(ENDPOINT_HALT) through ClearHalt(), which resets its expected PID to DATA0 as the
// device does (USB 2.0 section 9.4.5).
class SimFsdevHost {
public:
	static constexpr uint32 ReceiveCapacity = 512;
	static constexpr uint32 MaxAttempts = 64;


	std::array<uint8, ReceiveCapacity> received{};
	uint32 receivedCount = 0;
	uint32 droppedPackets = 0;


private:
	std::array<bool, SimFsdevController::EndpointCount> inDataOne{};
	std::array<bool, SimFsdevController::EndpointCount> outDataOne{};


public:
	std::span<const uint8> Received() const {
		return std::span<const uint8>(received.data(), receivedCount);
	}


	// One IN token. interruptAfter runs the port's interrupt handler after the transaction, as the controller does; a test passes
	// false to leave a completed packet pending. length is the byte count of the packet the host got (0 unless it was ACKed).
	SimHandshake InPacket(TestFsdevPort& port, uint8 address, bool interruptAfter, uint32& length) {
		uint8 number = USBEndpoint::Number(address);
		std::array<uint8, MaxPacketSize> packet{};
		bool dataOne = false;
		SimHandshake handshake = simFsdev.InToken(number, packet, length, dataOne);
		if (interruptAfter) {
			port.IrqHandler();
		}

		if (handshake != SimHandshake::Ack) {
			length = 0;
			return handshake;
		}

		if (dataOne != inDataOne[number]) {
			droppedPackets++;
			return handshake;
		}

		std::copy(packet.begin(), packet.begin() + length, received.begin() + receivedCount);
		receivedCount += length;
		inDataOne[number] = !inDataOne[number];
		return handshake;
	}


	// Reads an IN transfer: IN tokens until a short packet, a NAK or a STALL.
	SimHandshake ReadIn(TestFsdevPort& port, uint8 address, bool interruptAfter) {
		SimHandshake last = SimHandshake::None;
		for (uint32 attempt = 0; attempt < MaxAttempts; ++attempt) {
			uint32 length = 0;
			last = InPacket(port, address, interruptAfter, length);
			if (last != SimHandshake::Ack || length < MaxPacketSize) {
				return last;
			}
		}

		return last;
	}


	// One OUT packet with the PID the host expects. The host toggles on an ACK, and the device decides whether it stores the data.
	SimHandshake OutPacket(TestFsdevPort& port, uint8 address, std::span<const uint8> data, bool interruptAfter) {
		uint8 number = USBEndpoint::Number(address);
		SimHandshake handshake = simFsdev.OutToken(number, data, outDataOne[number]);
		if (interruptAfter) {
			port.IrqHandler();
		}

		if (handshake == SimHandshake::Ack) {
			outDataOne[number] = !outDataOne[number];
		}

		return handshake;
	}


	// Sends data as OUT packets of the maximum packet size, the last one short when the data does not divide evenly.
	SimHandshake WriteOut(TestFsdevPort& port, uint8 address, std::span<const uint8> data) {
		SimHandshake last = SimHandshake::Ack;
		for (std::size_t offset = 0; offset < data.size(); offset += MaxPacketSize) {
			std::size_t chunk = std::min<std::size_t>(MaxPacketSize, data.size() - offset);
			last = OutPacket(port, address, data.subspan(offset, chunk), true);
			if (last != SimHandshake::Ack) {
				return last;
			}
		}

		return last;
	}


	// CLEAR_FEATURE(ENDPOINT_HALT) on an address: the host expects DATA0 next, and the port clears the halt and the toggle.
	void ClearHalt(TestFsdevPort& port, uint8 address) {
		uint8 number = USBEndpoint::Number(address);
		if (USBEndpoint::IsIn(address)) {
			inDataOne[number] = false;
		} else {
			outDataOne[number] = false;
		}

		SIM_EXPECT(port.ClearStall(address) == ResultStatus::ok);
	}
};


// A token the host sends from inside a port call, at the EPnR write with index injectAt, counted from when the injection is armed.
// The hook runs before that write is applied (afterWrite false), so the token sees the register as it is between the port's read and
// its write; with afterWrite true it runs once the write is applied, so the token lands in the time the port spends after it.
struct TokenInjection {
	TestFsdevPort* port = nullptr;
	SimFsdevHost* host = nullptr;
	std::span<const uint8> data;
	uint8 address = 0;
	uint32 writesSeen = 0;
	uint32 injectAt = 0;
	bool isIn = false;
	SimHandshake result = SimHandshake::None;
};


TokenInjection injection;


// The EPnR write hook: counts the writes and runs the token at the chosen one.
void OnEprWrite() {
	injection.writesSeen++;
	if (injection.host == nullptr || injection.writesSeen != injection.injectAt) {
		return;
	}

	uint32 length = 0;
	if (injection.isIn) {
		injection.result = injection.host->InPacket(*injection.port, injection.address, false, length);
	} else {
		injection.result = injection.host->OutPacket(*injection.port, injection.address, injection.data, false);
	}
}


void ArmInjection(
	TestFsdevPort& port,
	SimFsdevHost& host,
	uint8 address,
	bool isIn,
	std::span<const uint8> data,
	uint32 injectAt,
	bool afterWrite = false)
{
	injection.port = &port;
	injection.host = &host;
	injection.address = address;
	injection.data = data;
	injection.isIn = isIn;
	injection.writesSeen = 0;
	injection.injectAt = injectAt;
	injection.result = SimHandshake::None;
	if (afterWrite) {
		simFsdev.afterEprWrite = &OnEprWrite;
	} else {
		simFsdev.beforeEprWrite = &OnEprWrite;
	}
}


void DisarmInjection() {
	simFsdev.beforeEprWrite = nullptr;
	simFsdev.afterEprWrite = nullptr;
	injection.host = nullptr;
	injection.port = nullptr;
}


std::array<uint8, TransferSize> MakeSource() {
	std::array<uint8, TransferSize> source{};
	for (uint32 index = 0; index < TransferSize; ++index) {
		source[index] = static_cast<uint8>((index * 37U + 11U) & 0xFF);
	}

	return source;
}


USBDeviceAdapter::EndpointConfig Config(uint8 address, USBEndpointType type, uint16 size, bool doubleBuffer) {
	USBDeviceAdapter::EndpointConfig config;
	config.address = address;
	config.type = type;
	config.maxPacketSize = size;
	config.doubleBuffer = doubleBuffer;
	return config;
}


// A fresh controller and port, connected. The packet memory and the registers start from the reset state.
void StartPort(TestFsdevPort& port) {
	simFsdev.Reset();
	USBDeviceAdapter::Parameters parameters;
	parameters.endpointCount = SimFsdevController::EndpointCount;
	SIM_EXPECT(port.SetParameters(parameters) == ResultStatus::ok);
	SIM_EXPECT(port.Connect() == ResultStatus::ok);
}


bool SameBytes(std::span<const uint8> expected, std::span<const uint8> actual) {
	return expected.size() == actual.size() && std::equal(expected.begin(), expected.end(), actual.begin());
}


// The IN transfer has arrived whole: every byte in order, no packet dropped, and one completion of the full size.
void ExpectInDelivered(const TestFsdevPort& port, const SimFsdevHost& host, uint8 address, std::span<const uint8> source) {
	SIM_EXPECT(host.droppedPackets == 0);
	SIM_EXPECT(SameBytes(source, host.Received()));
	SIM_EXPECT(!port.completions.overflowed);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	if (port.completions.count == 1) {
		SIM_EXPECT_EQ(port.completions.Back().address, address);
		SIM_EXPECT_EQ(port.completions.Back().byteCount, source.size());
	}
}


void ExpectOutDelivered(const TestFsdevPort& port, uint8 address, std::span<const uint8> source, std::span<const uint8> sink) {
	SIM_EXPECT(simFsdev.discardedOutPackets == 0);
	SIM_EXPECT(SameBytes(source, sink));
	SIM_EXPECT(!port.completions.overflowed);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	if (port.completions.count == 1) {
		SIM_EXPECT_EQ(port.completions.Back().address, address);
		SIM_EXPECT_EQ(port.completions.Back().byteCount, sink.size());
	}
}


// True while the controller holds a completed transfer on the endpoint: CTR_TX for IN, CTR_RX for OUT. The port's interrupt has not
// served it yet.
bool CompletionPending(uint8 address) {
	uint8 number = USBEndpoint::Number(address);
	uint16 flag = USBEndpoint::IsIn(address) ? USBFsdevEpr::CtrTx : USBFsdevEpr::CtrRx;
	return (simFsdev.ReadRegister(USBFsdevRegister::EpBase + 4U * number) & flag) != 0;
}


// Reads an IN transfer with the copy window modelled. The CPU takes each completion at once, but a further host token arrives after the
// first register write of that interrupt and before the port copies the next packet, since the copy takes time. Returns the number of
// tokens NAKed.
uint32 ReadInWithCopyWindow(TestFsdevPort& port, SimFsdevHost& host, uint8 address) {
	uint32 naks = 0;
	for (uint32 poll = 0; poll < MaxPolls && port.completions.count == 0; ++poll) {
		if (CompletionPending(address)) {
			ArmInjection(
				port,
				host,
				address,
				true,
				std::span<const uint8>(),
				1,
				true
			);
			port.IrqHandler();
			DisarmInjection();
			if (injection.result == SimHandshake::Nak) {
				naks++;
			}

			continue;
		}

		uint32 length = 0;
		if (host.InPacket(port, address, false, length) == SimHandshake::Nak) {
			naks++;
		}
	}

	return naks;
}


// The OUT counterpart of ReadInWithCopyWindow(): the host sends data as packets, a further packet arrives in the copy window of each
// interrupt. Returns the number of packets NAKed.
uint32 WriteOutWithCopyWindow(TestFsdevPort& port, SimFsdevHost& host, uint8 address, std::span<const uint8> data) {
	uint32 naks = 0;
	std::size_t offset = 0;
	for (uint32 poll = 0; poll < MaxPolls && port.completions.count == 0; ++poll) {
		std::size_t chunk = std::min<std::size_t>(MaxPacketSize, data.size() - offset);
		if (CompletionPending(address)) {
			if (offset < data.size()) {
				ArmInjection(
					port,
					host,
					address,
					false,
					data.subspan(offset, chunk),
					1,
					true
				);
				port.IrqHandler();
				DisarmInjection();
				if (injection.result == SimHandshake::Ack) {
					offset += chunk;
				} else if (injection.result == SimHandshake::Nak) {
					naks++;
				}
			} else {
				port.IrqHandler();
			}

			continue;
		}

		if (offset < data.size()) {
			SimHandshake handshake = host.OutPacket(port, address, data.subspan(offset, chunk), false);
			if (handshake == SimHandshake::Ack) {
				offset += chunk;
			} else if (handshake == SimHandshake::Nak) {
				naks++;
			}
		}
	}

	return naks;
}


// Reads an IN transfer with interrupt latency of one further token: the interrupt for a completion is taken after the next host token
// has arrived, as a CPU that is busy elsewhere takes it. The interrupt serves every completion pending at that time.
void ReadInWithLatency(TestFsdevPort& port, SimFsdevHost& host, uint8 address) {
	bool latched = false;
	for (uint32 poll = 0; poll < MaxPolls && port.completions.count == 0; ++poll) {
		uint32 length = 0;
		SimHandshake handshake = host.InPacket(port, address, false, length);
		if (latched) {
			port.IrqHandler();
			latched = false;
		}

		latched = handshake == SimHandshake::Ack;
	}
}


// Double-buffered bulk IN, four packets (64, 64, 64, 8), each handled as the port's interrupt reports it.
void TestInDoubleBufferDeliversEveryPacket() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	SIM_EXPECT(host.ReadIn(port, 0x81, true) == SimHandshake::Ack);
	ExpectInDelivered(port, host, 0x81, source);
}


// Double-buffered bulk OUT, four packets (64, 64, 64, 8), each stored by the port and acknowledged.
void TestOutDoubleBufferDeliversEveryPacket() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, TransferSize> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);

	SimFsdevHost host;
	SIM_EXPECT(host.WriteOut(port, 0x01, source) == SimHandshake::Ack);
	ExpectOutDelivered(port, 0x01, source, sink);
}


// CLEAR_FEATURE(HALT) after the first IN packet. The packet armed in buffer 1 is restarted in buffer 0 as DATA0, and the rest of
// the transfer follows with the host expecting DATA0 too.
void TestInClearHaltMidTransfer() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(host.InPacket(port, 0x81, true, length) == SimHandshake::Ack);
	host.ClearHalt(port, 0x81);
	SIM_EXPECT(!simFsdev.GetDataToggle(0x81));
	SIM_EXPECT(host.ReadIn(port, 0x81, true) == SimHandshake::Ack);
	ExpectInDelivered(port, host, 0x81, source);
}


// CLEAR_FEATURE(HALT) while the first IN packet is sent but its completion is still pending. The completion is taken first, the
// next packet is armed, and that one is restarted as DATA0.
void TestInClearHaltWithCompletedPacketPending() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(host.InPacket(port, 0x81, false, length) == SimHandshake::Ack);
	host.ClearHalt(port, 0x81);
	SIM_EXPECT(!simFsdev.GetDataToggle(0x81));
	SIM_EXPECT(host.ReadIn(port, 0x81, true) == SimHandshake::Ack);
	ExpectInDelivered(port, host, 0x81, source);
}


// CLEAR_FEATURE(HALT) after the first OUT packet, with the host expecting DATA0 again for the rest.
void TestOutClearHaltMidTransfer() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, TransferSize> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);

	SimFsdevHost host;
	std::span<const uint8> all(source);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(0, MaxPacketSize), true) == SimHandshake::Ack);
	host.ClearHalt(port, 0x01);
	SIM_EXPECT(!simFsdev.GetDataToggle(0x01));
	SIM_EXPECT(host.WriteOut(port, 0x01, all.subspan(MaxPacketSize)) == SimHandshake::Ack);
	ExpectOutDelivered(port, 0x01, source, sink);
}


// CLEAR_FEATURE(HALT) while the third OUT packet is stored but its completion is still pending. The port takes the packet, and
// the rest of the transfer starts at buffer 0 with DATA0.
void TestOutClearHaltWithCompletedPacketPending() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, TransferSize> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);

	SimFsdevHost host;
	std::span<const uint8> all(source);
	SIM_EXPECT(host.WriteOut(port, 0x01, all.subspan(0, 2 * MaxPacketSize)) == SimHandshake::Ack);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(2 * MaxPacketSize, MaxPacketSize), false) == SimHandshake::Ack);
	host.ClearHalt(port, 0x01);
	SIM_EXPECT(!simFsdev.GetDataToggle(0x01));
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(3 * MaxPacketSize), true) == SimHandshake::Ack);
	ExpectOutDelivered(port, 0x01, source, sink);
}


// Stall() while a packet is sent and the buffer conflict is set (the software has not armed the next buffer yet). The host gets
// STALL, the port reports the halt, and ClearStall() leaves the endpoint idle: the next IN token gets NAK.
void TestStallDuringConflictKeepsStallStatus() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(host.InPacket(port, 0x81, false, length) == SimHandshake::Ack);
	SIM_EXPECT(port.Stall(0x81) == ResultStatus::ok);
	SIM_EXPECT(host.InPacket(port, 0x81, true, length) == SimHandshake::Stall);
	SIM_EXPECT(port.IsStalled(0x81));
	SIM_EXPECT(port.ClearStall(0x81) == ResultStatus::ok);
	SIM_EXPECT(!port.IsStalled(0x81));
	SIM_EXPECT(host.InPacket(port, 0x81, true, length) == SimHandshake::Nak);
}


// The re-arm of a ClearStall() on a double-buffered IN endpoint with a packet armed. ClearStall() writes EPnR three times: the take-back,
// the re-arm of buffer 0, and the STAT. A token that arrives before the re-arm must find the buffer conflict and be NAKed, so the armed
// packet is not sent twice. Without the take-back in front, the same token is ACKed.
void TestInTokenBetweenTakeBackAndRearmIsNaked() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(host.InPacket(port, 0x81, true, length) == SimHandshake::Ack);
	ArmInjection(port, host, 0x81, true, std::span<const uint8>(), 2);
	host.ClearHalt(port, 0x81);
	DisarmInjection();
	SIM_EXPECT(injection.result == SimHandshake::Nak);
	SIM_EXPECT(host.ReadIn(port, 0x81, true) == SimHandshake::Ack);
	ExpectInDelivered(port, host, 0x81, source);
}


// The OUT counterpart: ClearStall() writes the take-back, the hand-back of buffer 0, and the STAT. A token between the take-back and
// the hand-back finds the conflict and is NAKed, so the host's packet is neither stored nor acknowledged.
void TestOutTokenBetweenTakeBackAndRearmIsNaked() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, TransferSize> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);

	SimFsdevHost host;
	std::span<const uint8> all(source);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(0, MaxPacketSize), true) == SimHandshake::Ack);
	ArmInjection(port, host, 0x01, false, all.subspan(MaxPacketSize, MaxPacketSize), 2);
	host.ClearHalt(port, 0x01);
	DisarmInjection();
	SIM_EXPECT(injection.result == SimHandshake::Nak);
	SIM_EXPECT(host.WriteOut(port, 0x01, all.subspan(MaxPacketSize)) == SimHandshake::Ack);
	ExpectOutDelivered(port, 0x01, source, sink);
}


// A single-buffered IN whose packet the hardware finished before a detach, and that the port has not served (no IrqHandler()). The
// endpoint is reopened double-buffered after the reconnect. The STAT copies must come from the register, or the first IN of the new
// transfer is NAKed.
void TestSingleInReopenedDoubleBufferAfterUnservedCompletion() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x82, USBEndpointType::Bulk, 64, false)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x82, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(host.InPacket(port, 0x82, false, length) == SimHandshake::Ack);
	SIM_EXPECT(port.Disconnect() == ResultStatus::ok);
	SIM_EXPECT(port.Connect() == ResultStatus::ok);
	SIM_EXPECT(port.OpenEndpoint(Config(0x82, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	SIM_EXPECT(port.StartInTransfer(0x82, source, false) == ResultStatus::ok);

	SimFsdevHost fresh;
	SIM_EXPECT(fresh.InPacket(port, 0x82, true, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, MaxPacketSize);
	SIM_EXPECT(fresh.droppedPackets == 0);
}


// Single-buffered bulk IN and OUT, the same four-packet transfers.
void TestSingleInDeliversEveryPacket() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x82, USBEndpointType::Bulk, 64, false)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x82, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	SIM_EXPECT(host.ReadIn(port, 0x82, true) == SimHandshake::Ack);
	ExpectInDelivered(port, host, 0x82, source);
}


void TestSingleOutDeliversEveryPacket() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Bulk, 64, false)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, TransferSize> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x02, sink) == ResultStatus::ok);

	SimFsdevHost host;
	SIM_EXPECT(host.WriteOut(port, 0x02, source) == SimHandshake::Ack);
	ExpectOutDelivered(port, 0x02, source, sink);
}


// Single-buffered CLEAR_FEATURE(HALT) mid-transfer. CLEAR_FEATURE(ENDPOINT_HALT) resets the data toggle to DATA0 (USB 2.0 section 9.4.5),
// so the re-armed single-buffered packet must go out as DATA0.
void TestSingleClearHaltMidTransfer() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x83, USBEndpointType::Bulk, 64, false)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x83, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(host.InPacket(port, 0x83, true, length) == SimHandshake::Ack);
	host.ClearHalt(port, 0x83);
	SIM_EXPECT(!simFsdev.GetDataToggle(0x83));
	SIM_EXPECT(host.ReadIn(port, 0x83, true) == SimHandshake::Ack);
	ExpectInDelivered(port, host, 0x83, source);
}


// Single-buffered Stall() on an armed transfer, then ClearStall(). Stall() ends the active transfer, so the next IN gets NAK.
void TestSingleStallThenClear() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x84, USBEndpointType::Bulk, 64, false)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x84, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(port.Stall(0x84) == ResultStatus::ok);
	SIM_EXPECT(host.InPacket(port, 0x84, true, length) == SimHandshake::Stall);
	SIM_EXPECT(port.ClearStall(0x84) == ResultStatus::ok);
	SIM_EXPECT(!port.IsStalled(0x84));
	SIM_EXPECT(host.InPacket(port, 0x84, true, length) == SimHandshake::Nak);
}


// Double-buffered IN with the CPU serving each completion at once and a host token arriving in the copy window of each interrupt. The
// port releases the prefilled buffer with the write that clears CTR_TX, before it fills the freed buffer
// (HandleDoubleBufferInCompletion(), USBFsdev.h). A host token in the copy window therefore finds a buffer ready and is ACKed.
void TestInNoNakInCopyWindow() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	SIM_EXPECT_EQ(ReadInWithCopyWindow(port, host, 0x81), 0U);
	ExpectInDelivered(port, host, 0x81, source);
}


// The OUT counterpart. The port hands the next buffer back with the write that clears CTR_RX, before it copies the packet out
// (HandleDoubleBufferOutCompletion(), USBFsdev.h), so a packet that arrives in the copy window is accepted.
void TestOutNoNakInCopyWindow() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, TransferSize> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);

	SimFsdevHost host;
	SIM_EXPECT_EQ(WriteOutWithCopyWindow(port, host, 0x01, source), 0U);
	ExpectOutDelivered(port, 0x01, source, sink);
}


// Double-buffered IN under interrupt latency of one further token. Each completion is served after the next token, which the
// one-buffer-ahead design cannot hide, so this case checks delivery only: the bytes arrive whole and in order.
void TestInDoubleBufferDeliversUnderInterruptLatency() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	ReadInWithLatency(port, host, 0x81);
	ExpectInDelivered(port, host, 0x81, source);
}


// A double-buffered OUT packet that arrives after the transfer is complete is held in packet memory. The 128-byte transfer
// completes with its second packet, the third packet lands in the buffer handed back at that completion and is held, and the
// fourth NAKs because no buffer is free. StartOutTransfer() copies the held packet first, and the host's retry completes the next
// transfer.
void TestOutHeldPacketCarriedToNextTransfer() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, 128> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);

	SimFsdevHost host;
	std::span<const uint8> all(source);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(0, 64), true) == SimHandshake::Ack);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(64, 64), true) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT(SameBytes(all.subspan(0, 128), sink));
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 128U);

	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(128, 64), true) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(192, 8), true) == SimHandshake::Nak);

	std::array<uint8, 72> nextSink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, nextSink) == ResultStatus::ok);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(192, 8), true) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT(SameBytes(all.subspan(128, 64), std::span<const uint8>(nextSink.data(), 64)));
	SIM_EXPECT(SameBytes(all.subspan(192, 8), std::span<const uint8>(nextSink.data() + 64, 8)));
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 72U);
	SIM_EXPECT(simFsdev.discardedOutPackets == 0);
}


// Close with a held OUT packet: the packet is dropped with the endpoint. The reopened endpoint receives only the new host's packet.
void TestOutHeldPacketDroppedByClose() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, 64> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);

	SimFsdevHost host;
	std::span<const uint8> all(source);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(0, 64), true) == SimHandshake::Ack);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(64, 64), true) == SimHandshake::Ack);
	SIM_EXPECT(port.CloseEndpoint(0x01) == ResultStatus::ok);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);

	std::array<uint8, 64> nextSink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, nextSink) == ResultStatus::ok);
	SimFsdevHost fresh;
	SIM_EXPECT(fresh.OutPacket(port, 0x01, all.subspan(128, 64), true) == SimHandshake::Ack);
	SIM_EXPECT(SameBytes(all.subspan(128, 64), nextSink));
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 64U);
	SIM_EXPECT(simFsdev.discardedOutPackets == 0);
}


// A held OUT packet that is short ends the next transfer as soon as StartOutTransfer() copies it out, so the completion is reported from
// that call. The endpoint then accepts nothing until the next StartOutTransfer().
void TestOutHeldShortPacketCompletesInStartOutTransfer() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, 64> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);

	SimFsdevHost host;
	std::span<const uint8> all(source);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(0, 64), true) == SimHandshake::Ack);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(64, 8), true) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);

	std::array<uint8, 64> nextSink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, nextSink) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 8U);
	SIM_EXPECT(SameBytes(all.subspan(64, 8), std::span<const uint8>(nextSink.data(), 8)));
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(72, 64), true) == SimHandshake::Nak);
	SIM_EXPECT(simFsdev.discardedOutPackets == 0);
}


// A port whose completions arm the next transfer from inside the callback, as a function's OnTransferComplete() does. Depth counts
// the callbacks that are open at once, so a test can see how far a chain of re-arms nests.
class TestRearmPort : public TestFsdevPort {
public:
	// Set by the test: the next completion arms rearmTarget and clears this flag.
	bool rearmOnNext = false;
	std::span<uint8> rearmTarget;
	ResultStatus rearmStatus = ResultStatus::notReady;
	uint32 depth = 0;
	uint32 maxDepth = 0;


protected:
	void CallTransferComplete(uint8 address, uint32 byteCount) override {
		depth++;
		if (depth > maxDepth) {
			maxDepth = depth;
		}

		TestFsdevPort::CallTransferComplete(address, byteCount);
		if (rearmOnNext) {
			rearmOnNext = false;
			rearmStatus = StartOutTransfer(address, rearmTarget);
		}

		depth--;
	}
};


// A held OUT packet is delivered by the next arm, from inside StartOutTransfer(), and the completion of that transfer arms the one
// after it from inside the callback. A packet is held while no transfer is armed. The interrupt can hold one, and so can
// StartOutTransfer() itself: HandleOutCompletion() runs in the call when the hardware already has a completed packet. The callbacks
// of this case nest one level, because the callback re-arms once and that arm delivers no more. A chain of re-arms that holds
// another packet each time nests deeper; the depth is bounded by the endpoint's buffers (USBDeviceAdapter::StartOutTransfer()),
// not by one level. The packets that follow are served from the interrupt, and the zero-length packet that ends the chain
// completes the transfer armed inside the callback.
void TestOutHeldPacketDeliveredByRearmingArm() {
	TestRearmPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::span<const uint8> all(source);
	std::array<uint8, 64> first{};
	std::array<uint8, 64> second{};
	std::array<uint8, 64> third{};
	std::array<uint8, 64> fourth{};
	std::array<uint8, 64> fifth{};
	SIM_EXPECT(port.StartOutTransfer(0x01, first) == ResultStatus::ok);

	SimFsdevHost host;
	// Packet A completes the armed transfer from the interrupt. Nothing is armed inside the callback.
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(0, 64), true) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT(SameBytes(all.subspan(0, 64), first));

	// Packet B arrives while nothing is armed: the endpoint accepts it into the freed buffer, and the interrupt holds it. A third
	// packet then finds both buffers owned by the software and is NAKed.
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(64, 64), true) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(128, 64), true) == SimHandshake::Nak);

	// The next arm takes B at once. Its completion arms the transfer that takes packet C, which the host then sends from the interrupt.
	port.rearmOnNext = true;
	port.rearmTarget = third;
	SIM_EXPECT(port.StartOutTransfer(0x01, second) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 64U);
	SIM_EXPECT(SameBytes(all.subspan(64, 64), second));
	SIM_EXPECT(port.rearmStatus == ResultStatus::ok);
	SIM_EXPECT_EQ(port.depth, 0U);
	SIM_EXPECT_EQ(port.maxDepth, 1U);

	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(128, 64), true) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 3U);
	SIM_EXPECT(SameBytes(all.subspan(128, 64), third));
	SIM_EXPECT_EQ(port.maxDepth, 1U);

	// Packet D (8 bytes, short) arrives with nothing armed and is held again. The next arm delivers it and ends the transfer at once.
	// The callback arms a transfer that a zero-length packet then completes.
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(192, 8), true) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 3U);
	port.rearmOnNext = true;
	port.rearmTarget = fourth;
	SIM_EXPECT(port.StartOutTransfer(0x01, fifth) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.completions.count, 4U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 8U);
	SIM_EXPECT(SameBytes(all.subspan(192, 8), std::span<const uint8>(fifth.data(), 8)));
	SIM_EXPECT(port.rearmStatus == ResultStatus::ok);
	SIM_EXPECT_EQ(port.maxDepth, 1U);

	SIM_EXPECT(host.OutPacket(port, 0x01, std::span<const uint8>(), true) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 5U);
	SIM_EXPECT_EQ(port.completions.Back().address, 0x01);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 0U);
	SIM_EXPECT(!port.completions.overflowed);
	SIM_EXPECT_EQ(port.maxDepth, 1U);
	SIM_EXPECT(simFsdev.discardedOutPackets == 0);
	SIM_EXPECT(host.droppedPackets == 0);
}


// A double-buffered OUT packet that the interrupt has not served when CLEAR_FEATURE(HALT) runs. The packet sits in buffer 0, and the
// reset of the toggles to DATA0 would make the interrupt read buffer 1 for it. ClearStall() serves the packet first, so it is held with
// the buffer it was stored in, and the next StartOutTransfer() delivers its 8 bytes.
void TestOutCompletedPacketServedAcrossClearHalt() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, 128> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);

	SimFsdevHost host;
	std::span<const uint8> all(source);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(0, 64), true) == SimHandshake::Ack);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(64, 64), true) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(128, 8), false) == SimHandshake::Ack);
	host.ClearHalt(port, 0x01);
	port.IrqHandler();

	std::array<uint8, 8> nextSink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, nextSink) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 8U);
	SIM_EXPECT(SameBytes(all.subspan(128, 8), std::span<const uint8>(nextSink)));
	SIM_EXPECT(simFsdev.discardedOutPackets == 0);
}


// A double-buffered OUT packet stored while the endpoint is idle, with its interrupt not yet served, when StartOutTransfer() runs. The
// packet is delivered first: StartOutTransfer() serves the completion before it hands a buffer back, so the next packet cannot take
// the buffer the held one is in. The next full packet is NAKed until the transfer after that.
void TestOutCompletedPacketServedBeforeStartOutTransfer() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::array<uint8, 64> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);

	SimFsdevHost host;
	std::span<const uint8> all(source);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(0, 64), true) == SimHandshake::Ack);
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(64, 8), false) == SimHandshake::Ack);

	std::array<uint8, 64> nextSink{};
	SIM_EXPECT(port.StartOutTransfer(0x01, nextSink) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 8U);
	SIM_EXPECT(SameBytes(all.subspan(64, 8), std::span<const uint8>(nextSink.data(), 8)));
	SIM_EXPECT(host.OutPacket(port, 0x01, all.subspan(72, 64), true) == SimHandshake::Nak);
	SIM_EXPECT(simFsdev.discardedOutPackets == 0);
}


// A zero-length packet after a transfer of two full packets, with both buffers in use: the host gets 64, 64 and the zero-length
// packet, and the completion reports the 128 bytes after the zero-length packet was sent.
void TestInZeroLengthPacketWithBothBuffers() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	std::span<const uint8> part(source.data(), 128);
	SIM_EXPECT(port.StartInTransfer(0x81, part, true) == ResultStatus::ok);

	SimFsdevHost host;
	SIM_EXPECT(host.ReadIn(port, 0x81, true) == SimHandshake::Ack);
	ExpectInDelivered(port, host, 0x81, part);
}


// An empty transfer on a double-buffered IN endpoint is one zero-length packet, and its completion follows that packet.
void TestInEmptyTransferSendsZeroLengthPacket() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(), false) == ResultStatus::ok);

	SimFsdevHost host;
	SIM_EXPECT(host.ReadIn(port, 0x81, true) == SimHandshake::Ack);
	ExpectInDelivered(port, host, 0x81, std::span<const uint8>());
}


// Stall() with both buffers in use (the first packet acknowledged, the second released, the third prefilled). The host gets STALL, the
// stall ends the transfer, and after ClearStall() a new transfer carries only its own bytes.
void TestInStallWithBothBuffersInUse() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(host.InPacket(port, 0x81, true, length) == SimHandshake::Ack);
	SIM_EXPECT(port.Stall(0x81) == ResultStatus::ok);
	SIM_EXPECT(host.InPacket(port, 0x81, true, length) == SimHandshake::Stall);
	SIM_EXPECT(port.IsStalled(0x81));
	host.ClearHalt(port, 0x81);
	SIM_EXPECT(!port.IsStalled(0x81));
	SIM_EXPECT(host.InPacket(port, 0x81, true, length) == SimHandshake::Nak);

	std::array<uint8, 100> second{};
	for (uint32 index = 0; index < second.size(); ++index) {
		second[index] = static_cast<uint8>(0xA5U ^ index);
	}

	SIM_EXPECT(port.StartInTransfer(0x81, second, false) == ResultStatus::ok);
	SimFsdevHost fresh;
	SIM_EXPECT(fresh.ReadIn(port, 0x81, true) == SimHandshake::Ack);
	ExpectInDelivered(port, fresh, 0x81, second);
}


// Close with both buffers in use: the transfer is abandoned without an event, and the reopened endpoint carries only the new transfer.
void TestInCloseWithBothBuffersInUse() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(host.InPacket(port, 0x81, true, length) == SimHandshake::Ack);
	SIM_EXPECT(port.CloseEndpoint(0x81) == ResultStatus::ok);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);

	std::array<uint8, 100> second{};
	for (uint32 index = 0; index < second.size(); ++index) {
		second[index] = static_cast<uint8>(0x3CU + index);
	}

	SIM_EXPECT(port.StartInTransfer(0x81, second, false) == ResultStatus::ok);
	SimFsdevHost fresh;
	SIM_EXPECT(fresh.ReadIn(port, 0x81, true) == SimHandshake::Ack);
	ExpectInDelivered(port, fresh, 0x81, second);
}


// Bus reset with both buffers in use: the reset closes every endpoint, and the reopened endpoint carries only the new transfer.
void TestInBusResetWithBothBuffersInUse() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(host.InPacket(port, 0x81, true, length) == SimHandshake::Ack);
	simFsdev.RaiseIstrFlags(USBFsdevIstr::Reset);
	port.IrqHandler();
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);

	std::array<uint8, 100> second{};
	for (uint32 index = 0; index < second.size(); ++index) {
		second[index] = static_cast<uint8>(0x77U ^ index);
	}

	SIM_EXPECT(port.StartInTransfer(0x81, second, false) == ResultStatus::ok);
	SimFsdevHost fresh;
	SIM_EXPECT(fresh.ReadIn(port, 0x81, true) == SimHandshake::Ack);
	ExpectInDelivered(port, fresh, 0x81, second);
}


// The release of a prefilled double-buffered IN buffer comes first in the interrupt. The register accesses before it are the ISTR read
// at entry and the EPnR read of the endpoint: the bound is 3 accesses, counting the release write itself.
constexpr uint32 ReleaseAccessBound = 3;
bool releaseSeen = false;
bool releaseApplied = false;
uint32 accessesAtRelease = 0;
// The endpoint's EPnR before the interrupt and after the first write of it. The endpoint is 1 (address 0x81).
constexpr uint32 ReleaseEndpointOffset = USBFsdevRegister::EpBase + 4U * 1U;
uint16 eprBeforeRelease = 0;
uint16 eprAfterRelease = 0;


// The first EPnR write of the interrupt, before it is applied: the accesses so far include this write.
void OnFirstEprWrite() {
	if (releaseSeen) {
		return;
	}

	releaseSeen = true;
	accessesAtRelease = simFsdev.registerReads + simFsdev.registerWrites;
}


// The same write, after it is applied. The EPnR read here is the controller's state, not a port access, so the counts are not used.
void OnFirstEprWriteApplied() {
	if (!releaseSeen || releaseApplied) {
		return;
	}

	releaseApplied = true;
	eprAfterRelease = simFsdev.ReadRegister(ReleaseEndpointOffset);
}


// A packet is sent and its completion is served late, as an interrupt latency does. The interrupt that serves it counts its register
// accesses up to the first EPnR write, which must be the release of the prefilled buffer within the bound. That write must clear CTR_TX,
// keep CTR_RX as it was and toggle DTOG_RX: a different first write would pass the count alone. The transfer then goes on.
void TestInReleaseIsFirstWriteInInterrupt() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	std::array<uint8, TransferSize> source = MakeSource();
	SIM_EXPECT(port.StartInTransfer(0x81, source, false) == ResultStatus::ok);

	SimFsdevHost host;
	uint32 length = 0;
	SIM_EXPECT(host.InPacket(port, 0x81, false, length) == SimHandshake::Ack);
	SIM_EXPECT(CompletionPending(0x81));

	eprBeforeRelease = simFsdev.ReadRegister(ReleaseEndpointOffset);
	simFsdev.ResetAccessCounts();
	releaseSeen = false;
	releaseApplied = false;
	accessesAtRelease = 0;
	eprAfterRelease = 0;
	simFsdev.beforeEprWrite = &OnFirstEprWrite;
	simFsdev.afterEprWrite = &OnFirstEprWriteApplied;
	port.IrqHandler();
	simFsdev.beforeEprWrite = nullptr;
	simFsdev.afterEprWrite = nullptr;
	std::printf("release write after %u register accesses (bound %u)\n", static_cast<unsigned>(accessesAtRelease), static_cast<unsigned>(ReleaseAccessBound));
	SIM_EXPECT(releaseSeen);
	SIM_EXPECT(releaseApplied);
	SIM_EXPECT(accessesAtRelease <= ReleaseAccessBound);
	SIM_EXPECT((eprAfterRelease & USBFsdevEpr::CtrTx) == 0);
	SIM_EXPECT((eprAfterRelease & USBFsdevEpr::CtrRx) == (eprBeforeRelease & USBFsdevEpr::CtrRx));
	SIM_EXPECT(((eprAfterRelease ^ eprBeforeRelease) & USBFsdevEpr::DtogRx) != 0);

	SIM_EXPECT(host.ReadIn(port, 0x81, true) == SimHandshake::Ack);
	ExpectInDelivered(port, host, 0x81, source);
}


}


// WaitMicroseconds() of the port multiplies SystemCoreClock. Zero makes its delay loops empty on the PC. The declaration in
// System/System.h gives it C linkage, and this definition keeps that linkage. This is the only test that includes the port.
uint32 SystemCoreClock = 0;


int main() {
	TestInDoubleBufferDeliversEveryPacket();
	TestOutDoubleBufferDeliversEveryPacket();
	TestInClearHaltMidTransfer();
	TestInClearHaltWithCompletedPacketPending();
	TestOutClearHaltMidTransfer();
	TestOutClearHaltWithCompletedPacketPending();
	TestStallDuringConflictKeepsStallStatus();
	TestSingleInDeliversEveryPacket();
	TestSingleOutDeliversEveryPacket();
	TestSingleClearHaltMidTransfer();
	TestSingleStallThenClear();
	TestInTokenBetweenTakeBackAndRearmIsNaked();
	TestOutTokenBetweenTakeBackAndRearmIsNaked();
	TestSingleInReopenedDoubleBufferAfterUnservedCompletion();
	TestInNoNakInCopyWindow();
	TestOutNoNakInCopyWindow();
	TestInDoubleBufferDeliversUnderInterruptLatency();
	TestOutHeldPacketCarriedToNextTransfer();
	TestOutHeldPacketDroppedByClose();
	TestOutHeldShortPacketCompletesInStartOutTransfer();
	TestOutHeldPacketDeliveredByRearmingArm();
	TestOutCompletedPacketServedAcrossClearHalt();
	TestOutCompletedPacketServedBeforeStartOutTransfer();
	TestInZeroLengthPacketWithBothBuffers();
	TestInEmptyTransferSendsZeroLengthPacket();
	TestInStallWithBothBuffersInUse();
	TestInCloseWithBothBuffersInUse();
	TestInBusResetWithBothBuffersInUse();
	TestInReleaseIsFirstWriteInInterrupt();
	return SimTest::Finish("TestFsdevPort");
}
