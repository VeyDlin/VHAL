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
	// The errors and the start-of-frame events the port raised, in order. The isochronous tests read them.
	FixedList<Error, 8> errors;
	FixedList<uint16, 16> frames;
	// Isochronous IN packets for 0x81, armed one after another by the completion callback, as a function arms its next packet from the
	// completion of the one before. isoInNext is the next one to arm.
	FixedList<std::span<const uint8>, 4> isoInQueue;
	std::size_t isoInNext = 0;
	// When set, the completion of the OUT on 0x02 arms outCompletionInPacket on the IN 0x81: an arm from another endpoint's callback.
	bool armInOnOutCompletion = false;
	std::span<const uint8> outCompletionInPacket;


	TestFsdevPort()
		: USBFsdev(SimFsdevController::RegisterBase) {
	}


	// The EPnR value of an endpoint as the port reads it.
	uint16 Epr(uint8 number) const {
		return ReadEpr(number);
	}


	// The COUNT of one buffer of an endpoint, without its block bits. Buffer 1 is the COUNT_RX field.
	uint16 Count(uint8 number, bool buffer1) const {
		return ReadBd(number, buffer1 ? USBFsdevBuffer::CountRx : USBFsdevBuffer::CountTx) & USBFsdevBuffer::CountMask;
	}


	// The packet memory address of one buffer of an endpoint. Buffer 1 is the ADDR_RX field.
	uint16 Address(uint8 number, bool buffer1) const {
		return ReadBd(number, buffer1 ? USBFsdevBuffer::AddrRx : USBFsdevBuffer::AddrTx);
	}


protected:
	void CallTransferComplete(uint8 address, uint32 byteCount) override {
		completions.Push(Completion{address, byteCount});
		if (address == 0x81 && isoInNext < isoInQueue.count) {
			ResultStatus status = StartInTransfer(0x81, isoInQueue[isoInNext], false);
			SIM_EXPECT(status == ResultStatus::ok);
			isoInNext++;
		}

		if (address == 0x02 && armInOnOutCompletion) {
			armInOnOutCompletion = false;
			SIM_EXPECT(StartInTransfer(0x81, outCompletionInPacket, false) == ResultStatus::ok);
		}
	}


	void CallError(Error error) override {
		errors.Push(error);
	}


	void CallStartOfFrame(uint16 frameNumber) override {
		frames.Push(frameNumber);
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


// Isochronous endpoints (see USBFsdev.h, ArmIsoIn(), HandleIsoInCompletion() and HandleIsoOutCompletion()). A frame is one token of the host:
// the controller answers it at once, as it does on the bus, and the port's interrupt runs after the token. An isochronous token is always
// answered, so the frame helpers return Ack for it. No PID is asserted, so the frame helpers do not return one, and the buffer select is read
// from EPnR as the bit that picks the buffer of the next packet.

constexpr uint32 IsoMaxPacket = 1023;
constexpr uint32 IsoPacketSize = 300;


// One IN token of a frame. The packet goes to packet, and length is its byte count: 0 for a zero-length packet, which is what a frame with
// nothing armed gets.
SimHandshake IsoInFrame(TestFsdevPort& port, uint8 address, std::span<uint8> packet, uint32& length) {
	bool dataOne = false;
	SimHandshake handshake = simFsdev.InToken(USBEndpoint::Number(address), packet, length, dataOne);
	port.IrqHandler();
	if (handshake != SimHandshake::Ack) {
		length = 0;
	}

	return handshake;
}


// One OUT token of a frame, carrying data. The controller stores the packet in buffer DTOG_RX selects whatever is armed, so Ack is the
// answer; the port then decides what the packet is worth (see TestIsoOutMissedFrameIsDropped()).
SimHandshake IsoOutFrame(TestFsdevPort& port, uint8 address, std::span<const uint8> data) {
	SimHandshake handshake = simFsdev.OutToken(USBEndpoint::Number(address), data, false);
	port.IrqHandler();
	return handshake;
}


// The buffer select of an isochronous IN address (DTOG_TX) and OUT address (DTOG_RX). It names the buffer of the next packet, and the model
// does not give it a PID.
bool InBufferSelect(const TestFsdevPort& port, uint8 address) {
	return (port.Epr(USBEndpoint::Number(address)) & USBFsdevEpr::DtogTx) != 0;
}


bool OutBufferSelect(const TestFsdevPort& port, uint8 address) {
	return (port.Epr(USBEndpoint::Number(address)) & USBFsdevEpr::DtogRx) != 0;
}


// A packet of size bytes with a pattern that depends on seed, so two packets of one stream differ.
std::array<uint8, IsoMaxPacket> MakeIsoPacket(uint8 seed) {
	std::array<uint8, IsoMaxPacket> packet{};
	for (uint32 index = 0; index < IsoMaxPacket; ++index) {
		packet[index] = static_cast<uint8>((index * 29U + seed * 53U + 7U) & 0xFF);
	}

	return packet;
}


// Isochronous IN, three packets (300, 300 and a short 17 bytes). The first is armed at stream start, and each completion arms the next from
// the callback (TestFsdevPort::CallTransferComplete()), so every packet is armed a frame ahead of its token. Each frame carries one packet whole,
// with one completion of its size, and the buffer select flips in every frame: buffer 0 in frame 0, buffer 1 in frame 1, and so on.
void TestIsoInDeliversOnePacketPerFrame() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, IsoPacketSize, false)) == ResultStatus::ok);

	std::array<uint8, IsoMaxPacket> first = MakeIsoPacket(1);
	std::array<uint8, IsoMaxPacket> second = MakeIsoPacket(2);
	std::array<uint8, IsoMaxPacket> third = MakeIsoPacket(3);
	std::span<const uint8> packets[] = {
		std::span<const uint8>(first).first(IsoPacketSize),
		std::span<const uint8>(second).first(IsoPacketSize),
		std::span<const uint8>(third).first(17)
	};

	port.isoInQueue.Push(packets[1]);
	port.isoInQueue.Push(packets[2]);
	// Armed from thread context, so the packet is deferred: the token of frame 0 finds nothing written, sends a zero-length packet and
	// writes the packet into the buffer it leaves free. The packet goes out at frame 1.
	SIM_EXPECT(port.StartInTransfer(0x81, packets[0], false) == ResultStatus::ok);

	// Frame 0 is the zero-length packet. Frames 1 to 3 carry the three packets, each armed by the completion callback of the one before, so
	// that packet goes out at the next token: the callback arm of packets[1] is sent at frame 2, and that of packets[2] at frame 3.
	std::span<const uint8> expected[4] = { std::span<const uint8>(), packets[0], packets[1], packets[2] };
	for (uint32 frame = 0; frame < 4; ++frame) {
		std::array<uint8, 1024> received{};
		uint32 length = 0;
		SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
		SIM_EXPECT_EQ(length, expected[frame].size());
		SIM_EXPECT(SameBytes(expected[frame], std::span<const uint8>(received).first(length)));
		SIM_EXPECT_EQ(port.completions.count, frame == 0 ? 0U : frame);
		if (frame > 0) {
			SIM_EXPECT_EQ(port.completions.Back().address, 0x81);
			SIM_EXPECT_EQ(port.completions.Back().byteCount, expected[frame].size());
		}

		if (frame == 0) {
			// The deferred write landed in buffer 1 at the zero-length token, and buffer 0 is still empty.
			SIM_EXPECT_EQ(port.Count(1, true), IsoPacketSize);
			SIM_EXPECT_EQ(port.Count(1, false), 0U);
		}

		// After frame f the select has flipped f + 1 times from its value at open (0), so it is set after an even frame index.
		SIM_EXPECT(InBufferSelect(port, 0x81) == ((frame % 2) == 0));
	}

	SIM_EXPECT_EQ(port.isoInNext, 2U);
	SIM_EXPECT(!port.completions.overflowed);
	SIM_EXPECT_EQ(port.errors.count, 0U);
}


// A frame with no packet armed is answered with a zero-length packet: the controller sends the buffer DTOG_TX selects, whose COUNT the port
// zeroed when that buffer's packet went out, so the stale packet is never sent again. The frame raises no completion, and it flips the buffer
// select like any other. A packet armed afterwards goes out at the next token, from the buffer the select now picks.
void TestIsoInFrameWithNothingArmedSendsZeroLengthPacket() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> first = MakeIsoPacket(4);
	std::array<uint8, IsoMaxPacket> second = MakeIsoPacket(5);
	std::array<uint8, 1024> received{};
	uint32 length = 0;

	// Frame 0: nothing armed. Buffer 0 has COUNT 0, so the token gets a zero-length packet, no completion, and the select goes to buffer 1.
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.completions.count, 0U);
	SIM_EXPECT_EQ(port.errors.count, 0U);
	SIM_EXPECT((port.Epr(1) & USBFsdevEpr::CtrTx) == 0);
	SIM_EXPECT(InBufferSelect(port, 0x81));

	// The first packet is armed from thread context. Nothing is copied yet: it waits for the next token.
	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(first).first(64), false) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);

	// Frame 1: the token finds buffer 1 empty and sends a zero-length packet. The deferred packet is written into buffer 0, which the
	// select now points to, and it completes nothing.
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.completions.count, 0U);
	SIM_EXPECT_EQ(port.Count(1, false), 64U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);
	SIM_EXPECT(!InBufferSelect(port, 0x81));

	// Frame 2: the first packet goes out from buffer 0. Its COUNT is zeroed after the send and the select goes to buffer 1.
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT(SameBytes(std::span<const uint8>(first).first(64), std::span<const uint8>(received).first(length)));
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT(InBufferSelect(port, 0x81));

	// Frame 3: nothing armed. Buffer 1 has COUNT 0, so the token gets a zero-length packet, not the packet of frame 2.
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT_EQ(port.errors.count, 0U);
	SIM_EXPECT(!InBufferSelect(port, 0x81));

	// Frames 4 and 5: the second packet is armed from thread context after frame 3. Frame 4 is its zero-length packet, which writes it into
	// buffer 1, and frame 5 sends it.
	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(second).first(64), false) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT_EQ(port.Count(1, true), 64U);
	SIM_EXPECT(InBufferSelect(port, 0x81));
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT(SameBytes(std::span<const uint8>(second).first(64), std::span<const uint8>(received).first(length)));
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT(!InBufferSelect(port, 0x81));
}


// Arming an isochronous IN writes no EPnR bit. STAT stays VALID from open, and SW_BUF (DTOG_RX) is left alone: the copy goes to the buffer the
// select picks and sets its COUNT, nothing else. The COUNTs are both 0 after open.
void TestIsoInArmWritesNoRegister() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);

	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);
	SIM_EXPECT((port.Epr(1) & USBFsdevEpr::TxStatus) == USBFsdevEpr::TxValid);
	SIM_EXPECT((port.Epr(1) & USBFsdevEpr::DtogRx) == 0);
	uint16 before = port.Epr(1);

	// Armed from thread context the packet is deferred: no register and no buffer is written by the call. The token that comes first writes it.
	std::array<uint8, IsoMaxPacket> packet = MakeIsoPacket(15);
	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(packet).first(64), false) == ResultStatus::ok);
	SIM_EXPECT(port.Epr(1) == before);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);

	std::array<uint8, 1024> received{};
	uint32 length = 0;
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 64U);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
}


// An isochronous endpoint has no EP_KIND: the bit is clear for isochronous and set for a double-buffered bulk endpoint, and a single-buffered
// bulk endpoint has it clear too.
void TestEpKindFollowsDoubleBuffer() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Bulk, 64, true)) == ResultStatus::ok);
	SIM_EXPECT(port.OpenEndpoint(Config(0x83, USBEndpointType::Bulk, 64, false)) == ResultStatus::ok);
	SIM_EXPECT((port.Epr(1) & USBFsdevEpr::Kind) == 0);
	SIM_EXPECT((port.Epr(2) & USBFsdevEpr::Kind) != 0);
	SIM_EXPECT((port.Epr(3) & USBFsdevEpr::Kind) == 0);
}


// Isochronous OUT, one packet per frame: a full one, a zero-length one (a completion of 0 bytes) and a short one. Each is stored and
// completed in its own frame, with the byte count the frame carried.
void TestIsoOutStoresOnePacketPerFrame() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, IsoPacketSize, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> data = MakeIsoPacket(6);
	std::array<uint8, IsoMaxPacket> sink{};

	SIM_EXPECT(port.StartOutTransfer(0x02, std::span<uint8>(sink).first(IsoPacketSize)) == ResultStatus::ok);
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(data).first(IsoPacketSize)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, IsoPacketSize);
	SIM_EXPECT(SameBytes(std::span<const uint8>(data).first(IsoPacketSize), std::span<const uint8>(sink).first(IsoPacketSize)));

	SIM_EXPECT(port.StartOutTransfer(0x02, std::span<uint8>(sink)) == ResultStatus::ok);
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>()) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 0U);

	SIM_EXPECT(port.StartOutTransfer(0x02, std::span<uint8>(sink)) == ResultStatus::ok);
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(data).first(17)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 3U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 17U);
	SIM_EXPECT(SameBytes(std::span<const uint8>(data).first(17), std::span<const uint8>(sink).first(17)));
	SIM_EXPECT(!port.completions.overflowed);
	SIM_EXPECT_EQ(port.errors.count, 0U);
}


// Isochronous OUT with no transfer armed for a frame. The controller stores the packet in buffer DTOG_RX selects, whatever is armed, and the
// port drops it: the frame is lost, nothing is delivered later, and no event is raised. The next armed transfer gets the packet of its own frame.
void TestIsoOutMissedFrameIsDropped() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> frame0 = MakeIsoPacket(7);
	std::array<uint8, IsoMaxPacket> frame1 = MakeIsoPacket(8);
	std::array<uint8, IsoMaxPacket> frame2 = MakeIsoPacket(9);
	std::array<uint8, IsoMaxPacket> sink{};

	SIM_EXPECT(port.StartOutTransfer(0x02, std::span<uint8>(sink).first(64)) == ResultStatus::ok);
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(frame0).first(64)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);

	// Frame 1 comes with nothing armed. The controller stores it in the buffer that frame 0 left the select on, and the port drops it: no
	// completion, no error, and the frame's packet is not kept.
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(frame1).first(64)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT(port.errors.count == 0);
	// The select moved in frame 1 as well: DTOG_RX went 1 -> 0 with the dropped packet.
	SIM_EXPECT(!OutBufferSelect(port, 0x02));

	SIM_EXPECT(port.StartOutTransfer(0x02, std::span<uint8>(sink).first(64)) == ResultStatus::ok);
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(frame2).first(64)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT(SameBytes(std::span<const uint8>(frame2).first(64), std::span<const uint8>(sink).first(64)));
	SIM_EXPECT(!port.completions.overflowed);
}


// Arming an isochronous OUT writes no EPnR bit: the hardware stores the next packet whatever is armed, so the arm only sets the buffer.
void TestIsoOutArmWritesNoRegister() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	SIM_EXPECT((port.Epr(2) & USBFsdevEpr::RxStatus) == USBFsdevEpr::RxValid);
	SIM_EXPECT((port.Epr(2) & USBFsdevEpr::Kind) == 0);
	uint16 before = port.Epr(2);

	std::array<uint8, 64> sink{};
	SIM_EXPECT(port.StartOutTransfer(0x02, sink) == ResultStatus::ok);
	SIM_EXPECT(port.Epr(2) == before);
	SIM_EXPECT(!OutBufferSelect(port, 0x02));
}


// An isochronous OUT packet longer than its block is not written past the block, and the port drops it. The sink is 1024 bytes, so the room
// left in the buffer is not the reason for the drop: only the block guard can refuse the 200-byte packet, whose COUNT is 200 against a
// 64-byte block, so the completion is 0 bytes with DataOverrun. Without the guard the port would copy 200 bytes from the block, 136 of them
// past its end. The sentinel after the block checks the simulator's clamp (SimFsdevController::OutToken() writes only the block and counts
// oversizedOutPackets), so it shows that the model stored no bytes past the block; the port's answer is the completion above.
void TestIsoOutOversizedPacketNeverWritesPastBlock() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> data = MakeIsoPacket(16);
	std::array<uint8, 1024> sink{};

	// The first OUT packet of the stream goes to buffer 0 (DTOG_RX is 0 after open), at ADDR_TX. The sentinel covers the 200 bytes after its block.
	uint16 block = port.Address(2, false);
	constexpr uint8 Sentinel = 0xA5;
	constexpr uint32 Written = 200;
	for (uint32 index = 0; index < Written; ++index) {
		simFsdev.packetMemory[block + 64 + index] = Sentinel;
	}

	SIM_EXPECT(port.StartOutTransfer(0x02, sink) == ResultStatus::ok);
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(data).first(200)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(simFsdev.oversizedOutPackets, 1U);
	SIM_EXPECT_EQ(port.errors.count, 1U);
	if (port.errors.count == 1) {
		SIM_EXPECT(port.errors.Back() == USBDeviceAdapter::Error::DataOverrun);
	}

	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 0U);
	SIM_EXPECT(sink[0] == 0);
	for (uint32 index = 0; index < Written; ++index) {
		SIM_EXPECT(simFsdev.packetMemory[block + 64 + index] == Sentinel);
	}
}


// An isochronous OUT packet that does not fit the armed buffer is dropped whole, with DataOverrun, and the transfer completes with 0 bytes.
void TestIsoOutOverrunDropsPacket() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> data = MakeIsoPacket(10);
	std::array<uint8, IsoMaxPacket> sink{};

	SIM_EXPECT(port.StartOutTransfer(0x02, std::span<uint8>(sink).first(32)) == ResultStatus::ok);
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(data).first(64)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.errors.count, 1U);
	if (port.errors.count == 1) {
		SIM_EXPECT(port.errors.Back() == USBDeviceAdapter::Error::DataOverrun);
	}

	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT_EQ(port.completions.Back().byteCount, 0U);
	SIM_EXPECT(sink[0] == 0);
}


// Stall() is refused on an isochronous endpoint, and ClearStall() is acknowledged and changes nothing: the endpoint is never halted and has no
// toggle to reset. The EPnR value is the same before and after the ClearStall(), and the packet armed before it still goes out, at the
// second token after the arm.
void TestIsoStallRefusedClearStallAcknowledged() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> data = MakeIsoPacket(11);
	std::array<uint8, 1024> received{};
	uint32 length = 0;

	SIM_EXPECT(port.Stall(0x81) == ResultStatus::invalidParameter);
	SIM_EXPECT(!port.IsStalled(0x81));
	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(data).first(64), false) == ResultStatus::ok);
	uint16 before = port.Epr(1);
	SIM_EXPECT(port.ClearStall(0x81) == ResultStatus::ok);
	SIM_EXPECT(port.Epr(1) == before);
	SIM_EXPECT(!port.IsStalled(0x81));
	// The packet was armed from thread context, so the first token sends a zero-length packet and the packet goes out at the second.
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.completions.count, 0U);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT(SameBytes(std::span<const uint8>(data).first(64), std::span<const uint8>(received).first(length)));

	// A direction that is not open is notExist, as for the other types.
	SIM_EXPECT(port.ClearStall(0x82) == ResultStatus::notExist);
}


// The open rules of an isochronous endpoint: the packet size range, the refused doubleBuffer flag, the sharing of the number's register with
// the other direction, and the packet memory (two blocks of the packet size, rounded to 32 bytes above 62).
void TestIsoOpenRules() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 0, false)) == ResultStatus::invalidParameter);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 1024, false)) == ResultStatus::invalidParameter);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, true)) == ResultStatus::invalidParameter);
	// Two blocks of 1024 bytes do not fit the 832 bytes left after the buffer table and endpoint 0.
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 1023, false)) == ResultStatus::outOfMemory);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, IsoPacketSize, false)) == ResultStatus::ok);

	// The other direction of the number is a bulk endpoint: the type is shared, so it is refused.
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Bulk, 64, false)) == ResultStatus::invalidParameter);
	// An isochronous OUT beside the open IN is double-buffered, which the register cannot hold beside the other direction.
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Isochronous, 64, false)) == ResultStatus::notSupported);
}


// The largest isochronous packet beside endpoint 0. The buffer table takes 64 bytes and endpoint 0 takes 128, which leaves 832. Two blocks of
// 416 bytes fill them exactly: the first at 192, the second at 608, ending at 1024. A 417-byte packet rounds to a 448 block, and two of those
// need 896, so the port refuses it with outOfMemory and keeps the 416-byte endpoint as it was.
void TestIsoLargestPacketBesideEndpointZero() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 416, false)) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.Address(1, false), 192U);
	SIM_EXPECT_EQ(port.Address(1, true), 608U);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 417, false)) == ResultStatus::outOfMemory);
	SIM_EXPECT_EQ(port.Address(1, false), 192U);
	SIM_EXPECT_EQ(port.Address(1, true), 608U);

	std::array<uint8, IsoMaxPacket> data = MakeIsoPacket(17);
	std::array<uint8, IsoMaxPacket> received{};
	uint32 length = 0;
	// Armed from thread context: the 416 bytes are written at the first token, which sends a zero-length packet, and go out at the second.
	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(data).first(416), false) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 416U);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 416U);
	SIM_EXPECT(SameBytes(std::span<const uint8>(data).first(416), std::span<const uint8>(received).first(length)));
}


// Packet memory of an isochronous endpoint is two blocks of its packet size, rounded to 32 bytes above 62 (300 bytes take 2 * 320). Closing
// an endpoint gives its blocks back for the next one.
void TestIsoPacketMemoryAndClose() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, IsoPacketSize, false)) == ResultStatus::ok);
	SIM_EXPECT(port.OpenEndpoint(Config(0x82, USBEndpointType::Isochronous, IsoPacketSize, false)) == ResultStatus::outOfMemory);
	SIM_EXPECT(port.CloseEndpoint(0x81) == ResultStatus::ok);
	SIM_EXPECT(port.OpenEndpoint(Config(0x82, USBEndpointType::Isochronous, IsoPacketSize, false)) == ResultStatus::ok);
}


// The start-of-frame event of the port, with an isochronous packet in the same interrupt: the event is raised and the packet completes.
void TestIsoStartOfFrameWithPacket() {
	TestFsdevPort port;
	simFsdev.Reset();
	USBDeviceAdapter::Parameters parameters;
	parameters.endpointCount = SimFsdevController::EndpointCount;
	parameters.startOfFrameEvents = true;
	SIM_EXPECT(port.SetParameters(parameters) == ResultStatus::ok);
	SIM_EXPECT(port.Connect() == ResultStatus::ok);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> data = MakeIsoPacket(12);
	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(data).first(64), false) == ResultStatus::ok);

	simFsdev.RaiseIstrFlags(USBFsdevIstr::Sof);
	std::array<uint8, 1024> received{};
	uint32 length = 0;
	// The packet was armed from thread context, so this frame's token sends a zero-length packet and the event is raised for the frame.
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.frames.count, 1U);
	SIM_EXPECT_EQ(port.completions.count, 0U);

	// The interrupt cleared the flag, so the next frame raises no start-of-frame until the controller sets it again. The packet goes out here.
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT_EQ(port.frames.count, 1U);
	SIM_EXPECT_EQ(port.completions.count, 1U);

	// Armed again from thread context: the zero-length token comes first, then the packet, and no start-of-frame is raised for either.
	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(data).first(64), false) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT_EQ(port.frames.count, 1U);
	SIM_EXPECT_EQ(port.completions.count, 2U);
}


// The IN 0x81 is armed from the completion of the OUT 0x02, which is another endpoint's callback. The write waits for the IN's next token: the
// first token sends a zero-length packet and writes the packet, and the packet goes out at the second token.
void TestIsoInArmFromOtherEndpointCallbackIsDeferred() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> packet = MakeIsoPacket(18);
	std::array<uint8, IsoMaxPacket> data = MakeIsoPacket(19);
	std::array<uint8, 64> sink{};
	std::array<uint8, 1024> received{};
	uint32 length = 0;

	port.outCompletionInPacket = std::span<const uint8>(packet).first(64);
	port.armInOnOutCompletion = true;
	SIM_EXPECT(port.StartOutTransfer(0x02, sink) == ResultStatus::ok);
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(data).first(64)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT_EQ(port.completions.Back().address, 0x02);

	// The arm happened in the OUT's callback, which is not the IN's own, so nothing is copied into the IN buffers yet.
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);

	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT(SameBytes(std::span<const uint8>(packet).first(64), std::span<const uint8>(received).first(length)));
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT_EQ(port.completions.Back().address, 0x81);
}


// While a packet is pending (not yet written) and while it is armed (written, waiting for its token), StartInTransfer() is busy. A new packet
// is taken only after the one before has completed.
void TestIsoInBusyWhilePendingOrArmed() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> packet = MakeIsoPacket(20);
	std::span<const uint8> first = std::span<const uint8>(packet).first(64);
	std::array<uint8, 1024> received{};
	uint32 length = 0;

	SIM_EXPECT(port.StartInTransfer(0x81, first, false) == ResultStatus::ok);
	SIM_EXPECT(port.StartInTransfer(0x81, first, false) == ResultStatus::busy);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);

	// The first token writes the packet: it is armed now, and still busy.
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 64U);
	SIM_EXPECT(port.StartInTransfer(0x81, first, false) == ResultStatus::busy);

	// The second token sends it. The completion ends the transfer, and the endpoint takes a new packet.
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	SIM_EXPECT(port.StartInTransfer(0x81, first, false) == ResultStatus::ok);
}


// Disconnect() drops a deferred packet and an armed one with no event, and empties the IN buffers. Each token after the reconnect sends a
// zero-length packet, so neither the dropped packet nor a stale one goes out.
void TestIsoInDisconnectDropsPendingAndArmedPackets() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> packet = MakeIsoPacket(21);
	std::span<const uint8> first = std::span<const uint8>(packet).first(64);
	std::array<uint8, 1024> received{};
	uint32 length = 0;

	// A deferred packet, dropped before any token.
	SIM_EXPECT(port.StartInTransfer(0x81, first, false) == ResultStatus::ok);
	SIM_EXPECT(port.Disconnect() == ResultStatus::ok);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);
	SIM_EXPECT(port.Connect() == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.completions.count, 0U);

	// An armed packet, written by a token and dropped by the detach before its own token.
	SIM_EXPECT(port.StartInTransfer(0x81, first, false) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.Count(1, true), 64U);
	SIM_EXPECT(port.Disconnect() == ResultStatus::ok);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);
	SIM_EXPECT(port.Connect() == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.completions.count, 0U);
	SIM_EXPECT_EQ(port.errors.count, 0U);
}


// CloseEndpoint() drops a deferred packet and an armed one, and zeroes the IN buffers, so a reopened endpoint sends zero-length packets only.
void TestIsoInCloseDropsPendingAndArmedPackets() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> packet = MakeIsoPacket(22);
	std::span<const uint8> first = std::span<const uint8>(packet).first(64);
	std::array<uint8, 1024> received{};
	uint32 length = 0;

	// A deferred packet, dropped by the close before any token.
	SIM_EXPECT(port.StartInTransfer(0x81, first, false) == ResultStatus::ok);
	SIM_EXPECT(port.CloseEndpoint(0x81) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);

	// An armed packet, written by a token and dropped by the close before its own token.
	SIM_EXPECT(port.StartInTransfer(0x81, first, false) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.Count(1, true), 64U);
	SIM_EXPECT(port.CloseEndpoint(0x81) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.completions.count, 0U);
	SIM_EXPECT_EQ(port.errors.count, 0U);
}


// A bus reset closes every endpoint, which drops an armed packet and empties the IN buffers. The reopened endpoint sends zero-length packets.
void TestIsoInBusResetDropsArmedPacket() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> packet = MakeIsoPacket(23);
	std::array<uint8, 1024> received{};
	uint32 length = 0;

	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(packet).first(64), false) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.Count(1, true), 64U);

	simFsdev.RaiseIstrFlags(USBFsdevIstr::Reset);
	port.IrqHandler();
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.completions.count, 0U);
}


// The isochronous OUT completion toggles DTOG_TX once (ST's PCD_FREE_USER_BUFFER), in the write that clears CTR_RX. The hardware's own
// changes are DTOG_RX and CTR_RX, so after one frame the EPnR differs from its value before the frame by exactly those two toggles and
// the clear of CTR_RX.
void TestIsoOutCompletionTogglesDtogTxOnce() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> data = MakeIsoPacket(24);
	std::array<uint8, 64> sink{};

	SIM_EXPECT(port.StartOutTransfer(0x02, sink) == ResultStatus::ok);
	uint16 before = port.Epr(2);
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(data).first(64)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	uint16 after = port.Epr(2);
	SIM_EXPECT((after & USBFsdevEpr::CtrRx) == 0);
	SIM_EXPECT(after == static_cast<uint16>(before ^ (USBFsdevEpr::DtogRx | USBFsdevEpr::DtogTx)));

	// The next frame toggles DTOG_TX back, once more.
	SIM_EXPECT(port.StartOutTransfer(0x02, sink) == ResultStatus::ok);
	uint16 middle = port.Epr(2);
	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(data).first(64)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 2U);
	SIM_EXPECT(port.Epr(2) == static_cast<uint16>(middle ^ (USBFsdevEpr::DtogRx | USBFsdevEpr::DtogTx)));
}


// Stall() answers notExist for a direction that is not open, before it answers the isochronous refusal. Number 1 is open as an isochronous
// IN, so its OUT direction is closed: Stall(0x01) is notExist, and Stall(0x81) is the refusal.
void TestIsoStallOnClosedDirectionIsNotExist() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	SIM_EXPECT(port.Stall(0x01) == ResultStatus::notExist);
	SIM_EXPECT(port.Stall(0x81) == ResultStatus::invalidParameter);
	SIM_EXPECT(!port.IsStalled(0x01));
	SIM_EXPECT(!port.IsStalled(0x81));
}


// The probe of TestIsoInCloseDisablesStatBeforeEmptyingBuffers(): the first EPnR write that CloseEndpoint() makes is observed after it is
// applied, with the status and the COUNTs the controller holds at that moment.
TestFsdevPort* closeProbePort = nullptr;
bool closeProbeTaken = false;
uint16 closeProbeStatus = 0;
uint32 closeProbeCount = 0;


void ProbeFirstCloseWrite() {
	if (closeProbeTaken || closeProbePort == nullptr) {
		return;
	}

	closeProbeTaken = true;
	closeProbeStatus = static_cast<uint16>(closeProbePort->Epr(1) & USBFsdevEpr::TxStatus);
	closeProbeCount = closeProbePort->Count(1, false) + closeProbePort->Count(1, true);
}


// CloseEndpoint() of an isochronous IN writes STAT DISABLED first and empties the COUNTs after it, so the controller answers no token for the
// endpoint from the first write on, and the buffers are emptied after that. The packet is still in its buffer when the STAT write is applied.
void TestIsoInCloseDisablesStatBeforeEmptyingBuffers() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> packet = MakeIsoPacket(25);
	std::array<uint8, 1024> received{};
	uint32 length = 0;

	// Armed from thread context: the first token writes the packet into the buffer DTOG_TX selects, and nothing completes.
	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(packet).first(64), false) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.Count(1, true), 64U);

	closeProbePort = &port;
	closeProbeTaken = false;
	simFsdev.afterEprWrite = &ProbeFirstCloseWrite;
	SIM_EXPECT(port.CloseEndpoint(0x81) == ResultStatus::ok);
	simFsdev.afterEprWrite = nullptr;
	closeProbePort = nullptr;
	SIM_EXPECT(closeProbeTaken);
	SIM_EXPECT_EQ(closeProbeStatus, USBFsdevEpr::TxDisabled);
	SIM_EXPECT_EQ(closeProbeCount, 64U);
	SIM_EXPECT_EQ(port.Count(1, false), 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 0U);
	SIM_EXPECT(port.completions.count == 0);
}


// A completion the controller holds from before CloseEndpoint() is not served against the next transfer of the endpoint. The host takes a
// bulk IN packet and the interrupt is not served, so CTR_TX stays set. The endpoint is closed and reopened, and a packet is armed. OpenEndpoint()
// clears the flag of the direction, so the interrupt that follows finds nothing to complete: the new packet is taken by the host, and only then
// does it complete.
void TestBulkInStaleCompletionAfterReopen() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x83, USBEndpointType::Bulk, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> firstPacket = MakeIsoPacket(26);
	std::array<uint8, IsoMaxPacket> secondPacket = MakeIsoPacket(27);
	SimFsdevHost host;
	uint32 length = 0;

	SIM_EXPECT(port.StartInTransfer(0x83, std::span<const uint8>(firstPacket).first(64), false) == ResultStatus::ok);
	SIM_EXPECT(host.InPacket(port, 0x83, false, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT(CompletionPending(0x83));
	SIM_EXPECT(port.CloseEndpoint(0x83) == ResultStatus::ok);

	SIM_EXPECT(port.OpenEndpoint(Config(0x83, USBEndpointType::Bulk, 64, false)) == ResultStatus::ok);
	SIM_EXPECT(!CompletionPending(0x83));
	SIM_EXPECT(port.StartInTransfer(0x83, std::span<const uint8>(secondPacket).first(64), false) == ResultStatus::ok);
	port.IrqHandler();
	SIM_EXPECT_EQ(port.completions.count, 0U);

	// The reopened endpoint starts with DATA0, so the host that starts afresh takes the new packet, and that completes it.
	SimFsdevHost nextHost;
	SIM_EXPECT(nextHost.InPacket(port, 0x83, true, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT(!port.completions.overflowed);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	if (port.completions.count == 1) {
		SIM_EXPECT_EQ(port.completions.Back().address, 0x83);
		SIM_EXPECT_EQ(port.completions.Back().byteCount, 64U);
	}

	SIM_EXPECT(SameBytes(std::span<const uint8>(secondPacket).first(64), nextHost.Received()));
}


// The same for an isochronous OUT: the host's frame stores its packet and leaves CTR_RX set, the endpoint is closed and reopened and a transfer
// is armed, and the interrupt does not complete that transfer with the old packet. The next frame completes it with its own packet.
void TestIsoOutStaleCompletionAfterReopen() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> data = MakeIsoPacket(28);
	std::array<uint8, 64> sink{};
	std::array<uint8, 64> nextSink{};

	SIM_EXPECT(port.StartOutTransfer(0x02, sink) == ResultStatus::ok);
	SIM_EXPECT(simFsdev.OutToken(2, std::span<const uint8>(data).first(64), false) == SimHandshake::Ack);
	SIM_EXPECT(CompletionPending(0x02));
	SIM_EXPECT(port.CloseEndpoint(0x02) == ResultStatus::ok);

	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	SIM_EXPECT(!CompletionPending(0x02));
	SIM_EXPECT(port.StartOutTransfer(0x02, nextSink) == ResultStatus::ok);
	port.IrqHandler();
	SIM_EXPECT_EQ(port.completions.count, 0U);

	SIM_EXPECT(IsoOutFrame(port, 0x02, std::span<const uint8>(data).first(64)) == SimHandshake::Ack);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	if (port.completions.count == 1) {
		SIM_EXPECT_EQ(port.completions.Back().byteCount, 64U);
	}

	SIM_EXPECT(SameBytes(std::span<const uint8>(data).first(64), std::span<const uint8>(nextSink)));
}


// An isochronous IN completion that the interrupt has not served when the number is reopened as an isochronous OUT. CloseEndpoint() leaves
// CTR_TX set, and OpenEndpoint() of the OUT clears only CTR_RX. The interrupt serves CTR_TX first, so the IN handler runs against the OUT's
// buffers: it must clear its flag and leave the COUNTs alone. The OUT packet of the frame then completes with its bytes.
void TestIsoInStaleCompletionBeforeReopenAsOut() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> inPacket = MakeIsoPacket(29);
	std::array<uint8, IsoMaxPacket> outPacket = MakeIsoPacket(30);
	std::array<uint8, IsoMaxPacket> received{};
	std::array<uint8, 64> sink{};
	uint32 length = 0;

	// The first token sends the zero-length packet of a frame nobody armed, and the interrupt writes the packet into the buffer the next
	// token sends. That token runs without the interrupt, so CTR_TX stays set with the packet sent.
	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(inPacket).first(64), false) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(port.Count(1, true), 64U);
	bool dataOne = false;
	SIM_EXPECT(simFsdev.InToken(1, received, length, dataOne) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT(CompletionPending(0x81));
	SIM_EXPECT(port.CloseEndpoint(0x81) == ResultStatus::ok);

	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	SIM_EXPECT(CompletionPending(0x81));
	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);
	SIM_EXPECT(simFsdev.OutToken(1, std::span<const uint8>(outPacket).first(64), false) == SimHandshake::Ack);
	port.IrqHandler();
	SIM_EXPECT(port.errors.count == 0);
	SIM_EXPECT(!port.completions.overflowed);
	SIM_EXPECT_EQ(port.completions.count, 1U);
	if (port.completions.count == 1) {
		SIM_EXPECT_EQ(port.completions.Back().address, 0x01);
		SIM_EXPECT_EQ(port.completions.Back().byteCount, 64U);
	}

	SIM_EXPECT(SameBytes(std::span<const uint8>(outPacket).first(64), std::span<const uint8>(sink)));
	SIM_EXPECT(!CompletionPending(0x81));
}


// The reverse: a completion of a closed isochronous OUT that the interrupt has not served when the number is reopened as an isochronous IN.
// CTR_RX stays set through the close, and the IN's OpenEndpoint() clears only CTR_TX. The stale OUT handler toggles DTOG_TX, which is the buffer
// select of the IN, so a completion for a closed direction must leave it alone. The IN then goes on as a fresh endpoint.
void TestIsoOutStaleCompletionBeforeReopenAsIn() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x01, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	std::array<uint8, IsoMaxPacket> outPacket = MakeIsoPacket(31);
	std::array<uint8, IsoMaxPacket> inPacket = MakeIsoPacket(32);
	std::array<uint8, IsoMaxPacket> received{};
	std::array<uint8, 64> sink{};
	uint32 length = 0;

	SIM_EXPECT(port.StartOutTransfer(0x01, sink) == ResultStatus::ok);
	SIM_EXPECT(simFsdev.OutToken(1, std::span<const uint8>(outPacket).first(64), false) == SimHandshake::Ack);
	SIM_EXPECT(CompletionPending(0x01));
	SIM_EXPECT(port.CloseEndpoint(0x01) == ResultStatus::ok);

	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 64, false)) == ResultStatus::ok);
	SIM_EXPECT(CompletionPending(0x01));
	SIM_EXPECT(!InBufferSelect(port, 0x81));
	port.IrqHandler();
	SIM_EXPECT(!CompletionPending(0x01));
	SIM_EXPECT(!InBufferSelect(port, 0x81));
	SIM_EXPECT_EQ(port.completions.count, 0U);
	SIM_EXPECT_EQ(port.errors.count, 0U);

	SIM_EXPECT(port.StartInTransfer(0x81, std::span<const uint8>(inPacket).first(64), false) == ResultStatus::ok);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT(IsoInFrame(port, 0x81, received, length) == SimHandshake::Ack);
	SIM_EXPECT_EQ(length, 64U);
	SIM_EXPECT(SameBytes(std::span<const uint8>(inPacket).first(64), std::span<const uint8>(received).first(length)));
	SIM_EXPECT_EQ(port.completions.count, 1U);
}


// Two stereo 48 kHz isochronous endpoints of 196 bytes. A block is 224 bytes, the packet rounded up to 32 above 62 bytes, and an isochronous
// endpoint holds two blocks whichever direction it is. The buffer table takes 64 bytes and endpoint 0 takes 128, so the IN takes the blocks at
// 192 and 416, and the OUT finds 640 and 864 as its first and second. The second needs 864 + 224 = 1088, over the 1024 of the packet memory, so
// the OUT is refused with outOfMemory and the IN keeps its blocks. Closing the IN frees them, and then the OUT fits at 192 and 416.
void TestIsoStereoPairOutOfMemory() {
	TestFsdevPort port;
	StartPort(port);
	SIM_EXPECT(port.OpenEndpoint(Config(0x81, USBEndpointType::Isochronous, 196, false)) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.Address(1, false), 192U);
	SIM_EXPECT_EQ(port.Address(1, true), 416U);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, 196, false)) == ResultStatus::outOfMemory);
	SIM_EXPECT_EQ(port.Address(1, false), 192U);
	SIM_EXPECT_EQ(port.Address(1, true), 416U);

	SIM_EXPECT(port.CloseEndpoint(0x81) == ResultStatus::ok);
	SIM_EXPECT(port.OpenEndpoint(Config(0x02, USBEndpointType::Isochronous, 196, false)) == ResultStatus::ok);
	SIM_EXPECT_EQ(port.Address(2, false), 192U);
	SIM_EXPECT_EQ(port.Address(2, true), 416U);
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
	TestIsoInDeliversOnePacketPerFrame();
	TestIsoInFrameWithNothingArmedSendsZeroLengthPacket();
	TestIsoInArmWritesNoRegister();
	TestEpKindFollowsDoubleBuffer();
	TestIsoOutStoresOnePacketPerFrame();
	TestIsoOutMissedFrameIsDropped();
	TestIsoOutArmWritesNoRegister();
	TestIsoOutOversizedPacketNeverWritesPastBlock();
	TestIsoOutOverrunDropsPacket();
	TestIsoStallRefusedClearStallAcknowledged();
	TestIsoOpenRules();
	TestIsoLargestPacketBesideEndpointZero();
	TestIsoPacketMemoryAndClose();
	TestIsoStartOfFrameWithPacket();
	TestIsoInArmFromOtherEndpointCallbackIsDeferred();
	TestIsoInBusyWhilePendingOrArmed();
	TestIsoInDisconnectDropsPendingAndArmedPackets();
	TestIsoInCloseDropsPendingAndArmedPackets();
	TestIsoInBusResetDropsArmedPacket();
	TestIsoOutCompletionTogglesDtogTxOnce();
	TestIsoStallOnClosedDirectionIsNotExist();
	TestIsoInCloseDisablesStatBeforeEmptyingBuffers();
	TestBulkInStaleCompletionAfterReopen();
	TestIsoOutStaleCompletionAfterReopen();
	TestIsoInStaleCompletionBeforeReopenAsOut();
	TestIsoOutStaleCompletionBeforeReopenAsIn();
	TestIsoStereoPairOutOfMemory();
	return SimTest::Finish("TestFsdevPort");
}
