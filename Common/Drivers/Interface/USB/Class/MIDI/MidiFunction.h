#pragma once
#include <VHAL.h>
#include <Adapter/USBDeviceAdapter.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <System/System.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <Utilities/VSTD/utility/Delegate.h>
#include <array>
#include <cstddef>
#include <span>


// Code index numbers of the USB-MIDI event packets (MIDI 1.0 section 4, table 4-1). The low nibble of byte 0 of a packet.
constexpr uint8 USBMidiCinSysExStart = 0x4;
constexpr uint8 USBMidiCinSysExEnd1 = 0x5;
constexpr uint8 USBMidiCinSysExEnd2 = 0x6;
constexpr uint8 USBMidiCinSysExEnd3 = 0x7;
constexpr uint8 USBMidiCinNoteOff = 0x8;
constexpr uint8 USBMidiCinNoteOn = 0x9;
constexpr uint8 USBMidiCinPolyKeyPressure = 0xA;
constexpr uint8 USBMidiCinControlChange = 0xB;
constexpr uint8 USBMidiCinProgramChange = 0xC;
constexpr uint8 USBMidiCinChannelPressure = 0xD;
constexpr uint8 USBMidiCinPitchBend = 0xE;


// One USB-MIDI event packet: byte 0 carries the cable number in bits 7..4 and the code index number in bits 3..0, bytes 1
// to 3 are the MIDI bytes. Bytes the code index number does not use are zero.
struct USBMidiPacket {
	std::array<uint8, 4> bytes{};


	static constexpr USBMidiPacket Make(uint8 cable, uint8 codeIndex, uint8 byte1, uint8 byte2, uint8 byte3) {
		USBMidiPacket packet;
		packet.bytes[0] = static_cast<uint8>((cable << 4) | (codeIndex & 0x0F));
		packet.bytes[1] = byte1;
		packet.bytes[2] = byte2;
		packet.bytes[3] = byte3;
		return packet;
	}


	constexpr uint8 Cable() const {
		return static_cast<uint8>(bytes[0] >> 4);
	}


	constexpr uint8 CodeIndex() const {
		return static_cast<uint8>(bytes[0] & 0x0F);
	}
};


// The USB-MIDI function of the MIDIStreaming interface: one bulk IN and one bulk OUT endpoint, 64-byte packets, carrying
// 4-byte event packets. The interface span lists the MIDIStreaming interface, and the Audio Control interface in front of it
// when the builder emits one. OnConfigured() checks only that the span is not empty: the function cannot tell an Audio Control
// interface from a MIDIStreaming one, so the descriptors must group them. The endpoints span holds the bulk IN and the bulk OUT
// address in either order, as the vendor function's does.
//
// Send side. SendEvent() and the helpers queue packets from the thread, and Process() starts the IN transfer. Each IN transfer
// carries up to 16 whole packets, and the next transfer starts from the IN completion, so one transfer leaves the queue at
// each completion and the rest follows without Process(). A full 16-packet transfer ends with a zero-length packet only when no
// event waits behind it (see BuildInTransfer()). A queue that is full returns busy. Packets of a transfer that a halt
// or a failed start abandons go back to the queue, so the host reads them after the halt clears. A bus reset or a configuration
// clears the queue.
//
// Receive side. The OUT endpoint is armed only while the RX queue has room for one whole transfer, so the host keeps its
// packets when the thread is behind (the endpoint NAKs). ReadEvent() has no device context, so the application's Process() re-arms
// the endpoint after the reads. Each received packet goes to the RX queue (ReadEvent()) and to the
// event delegate, which runs in interrupt context. A SysEx packet also gives its bytes to the SysEx delegate as a byte
// stream, in the order received, so a SysEx message can be reassembled without the RX queue.
//
// Interrupt context: the completions, the halts, OnConfigured(), OnBusReset() and the delegates run in interrupt context.
// Process() and the send and read calls run in the thread. The thread calls take a critical section around the queue and
// transfer state. Sections nest, so they may call the ring buffers, and each one stays short.
//
// TxQueueEvents and RxQueueEvents count event packets. RxQueueEvents must hold one whole OUT transfer (16 packets).
template<uint8 Cables, uint16 TxQueueEvents, uint16 RxQueueEvents>
class MidiFunction : public USBFunction {
	static_assert(Cables >= 1 && Cables <= 16, "USB-MIDI cable numbers are 0 to 15, so a function has 1 to 16 cables");
	static_assert(TxQueueEvents >= 1, "The TX queue holds at least one event");
	static_assert(RxQueueEvents >= 16, "The RX queue must hold one whole OUT transfer (16 packets)");


public:
	static constexpr uint16 PacketSize = 64;
	static constexpr uint16 PacketsPerTransfer = PacketSize / 4;

	// Called with each received event, in interrupt context. The packet is valid only during the call.
	using EventHandler = VSTD::Delegate<void(const USBMidiPacket& packet)>;

	// Called with the MIDI bytes of each received SysEx packet, in interrupt context. The span is valid only during the call.
	// A packet carries one to three bytes; the F0 and F7 bytes of a message arrive as part of the stream.
	using SysExHandler = VSTD::Delegate<void(uint8 cable, std::span<const uint8> bytes)>;


private:
	RingBuffer<USBMidiPacket, TxQueueEvents> txRing;
	RingBuffer<USBMidiPacket, RxQueueEvents> rxRing;
	// Scratch of BuildInTransfer(). Only the holder of the txClaimed claim uses it.
	std::array<USBMidiPacket, PacketsPerTransfer> txPackets{};
	std::array<uint8, PacketSize> inBuffer{};
	std::array<uint8, PacketSize> outBuffer{};
	EventHandler eventHandler;
	SysExHandler sysExHandler;
	uint8 inAddress = 0;
	uint8 outAddress = 0;
	bool configured = false;
	// Set from the start of an IN transfer until its completion, or until the start or the claim is abandoned.
	bool inFlight = false;
	bool inHalted = false;
	// Set by TryStartIn() while it builds a transfer outside the critical section. A halt in that window only records itself, and
	// TryStartIn() returns the packets it marked. A bus reset in that window clears the queue and moves the session on, so
	// TryStartIn() starts nothing.
	bool txClaimed = false;
	bool outArmed = false;
	bool outHalted = false;
	// Count the OUT starts and the IN transfer changes (starts and completions), bumped before the adapter call. A start that returns
	// an error clears its flag, or gives back its marks, only when nothing ran inside its call that changed the same state: a
	// completion inside the call has already re-armed or committed it.
	uint32 outStarts = 0;
	uint32 inTransfers = 0;
	// Counts each configuration and bus reset. TryStartIn() compares it before it starts a transfer, so packets taken from the
	// queue before a reset never go out after it.
	uint32 sessionGeneration = 0;


public:
	MidiFunction(std::span<const uint8> interfaces, std::span<const uint8> endpoints)
		: USBFunction(interfaces, endpoints) {
	}


	// From the fluent builder's layout (USBDescriptors::Function()).
	explicit MidiFunction(const USBFunctionLayout& layout)
		: USBFunction(layout) {
	}


	// Sets the event handler. Returns busy while the function is configured, because the handler is called from the device's
	// context.
	ResultStatus SetEventHandler(EventHandler value) {
		System::CriticalSection(true);
		if (configured) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		eventHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	// Sets the SysEx handler. Returns busy while the function is configured.
	ResultStatus SetSysExHandler(SysExHandler value) {
		System::CriticalSection(true);
		if (configured) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		sysExHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	// Queues one event packet for the host. The packet must be a complete event for its code index (see IsValidEvent()), so SysEx
	// framing goes through SendSysEx(). Returns invalidParameter for a cable outside the function or a malformed packet, notReady
	// while the function is not configured, and busy when the queue is full.
	ResultStatus SendEvent(const USBMidiPacket& packet) {
		if (packet.Cable() >= Cables || !IsValidEvent(packet)) {
			return ResultStatus::invalidParameter;
		}

		// The configuration check and the push share one section, so a reset cannot land between them and queue the packet for a
		// session that has ended.
		ResultStatus status = ResultStatus::notReady;
		System::CriticalSection(true);
		if (configured) {
			status = txRing.Push(packet) == ResultStatus::ok ? ResultStatus::ok : ResultStatus::busy;
		}

		System::CriticalSection(false);
		return status;
	}


	// Channel messages. channel is 0 to 15 and each data byte is 0 to 127; a value out of range returns invalidParameter and
	// queues nothing. The three-byte messages (note, control change, pitch bend) and the two-byte ones use the same queue.
	ResultStatus NoteOff(uint8 cable, uint8 channel, uint8 note, uint8 velocity) {
		return SendChannel(cable, USBMidiCinNoteOff, 0x80, channel, note, velocity, true);
	}


	ResultStatus NoteOn(uint8 cable, uint8 channel, uint8 note, uint8 velocity) {
		return SendChannel(cable, USBMidiCinNoteOn, 0x90, channel, note, velocity, true);
	}


	ResultStatus PolyKeyPressure(uint8 cable, uint8 channel, uint8 note, uint8 pressure) {
		return SendChannel(cable, USBMidiCinPolyKeyPressure, 0xA0, channel, note, pressure, true);
	}


	ResultStatus ControlChange(uint8 cable, uint8 channel, uint8 controller, uint8 value) {
		return SendChannel(cable, USBMidiCinControlChange, 0xB0, channel, controller, value, true);
	}


	ResultStatus ProgramChange(uint8 cable, uint8 channel, uint8 program) {
		return SendChannel(cable, USBMidiCinProgramChange, 0xC0, channel, program, 0, false);
	}


	ResultStatus ChannelPressure(uint8 cable, uint8 channel, uint8 pressure) {
		return SendChannel(cable, USBMidiCinChannelPressure, 0xD0, channel, pressure, 0, false);
	}


	// value is the 14-bit pitch bend, 0 to 16383, with 8192 at centre. It is sent LSB first, as the MIDI message carries it.
	ResultStatus PitchBend(uint8 cable, uint8 channel, uint16 value) {
		if (value > 16383) {
			return ResultStatus::invalidParameter;
		}

		return SendChannel(cable, USBMidiCinPitchBend, 0xE0, channel, static_cast<uint8>(value & 0x7F), static_cast<uint8>(value >> 7), true);
	}


	// Queues a SysEx message as CIN 0x4 packets of three bytes and one closing packet (0x5, 0x6 or 0x7) with the last one to three
	// bytes. A message of N bytes takes ceil(N / 3) packets. A message is framed by 0xF0 and 0xF7, so it has at least two bytes.
	// The bytes after the first must be 7-bit, and a final 0xF7 is the only one with bit 7 set, because a status byte inside a
	// SysEx message ends it on the host. The message is queued whole or not at all: busy when the queue has no room for all of its
	// packets now, invalidParameter for a message not framed by 0xF0 and 0xF7, a status byte inside it, or one longer than the
	// queue, notReady while not configured.
	ResultStatus SendSysEx(uint8 cable, std::span<const uint8> bytes) {
		if (cable >= Cables || bytes.size() < 2 || bytes.front() != 0xF0 || bytes.back() != 0xF7) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 1; index < bytes.size(); index++) {
			bool closing = index == bytes.size() - 1 && bytes[index] == 0xF7;
			if (bytes[index] >= 0x80 && !closing) {
				return ResultStatus::invalidParameter;
			}
		}

		std::size_t packetCount = (bytes.size() + 2) / 3;
		if (packetCount > TxQueueEvents) {
			return ResultStatus::invalidParameter;
		}

		if (!configured) {
			return ResultStatus::notReady;
		}

		// The configuration check, the room check and the pushes share one section, so a reset cannot land between them. The
		// completion only removes packets, so the room counted here is still there for the pushes.
		ResultStatus status = ResultStatus::notReady;
		System::CriticalSection(true);
		if (configured) {
			status = txRing.GetFreeSpace() < packetCount ? ResultStatus::busy : ResultStatus::ok;
		}

		if (status == ResultStatus::ok) {
			std::size_t offset = 0;
			while (offset < bytes.size()) {
				std::size_t remaining = bytes.size() - offset;
				uint8 codeIndex = USBMidiCinSysExStart;
				std::size_t count = 3;
				if (remaining <= 3) {
					count = remaining;
					if (count == 3) {
						codeIndex = USBMidiCinSysExEnd3;
					} else if (count == 2) {
						codeIndex = USBMidiCinSysExEnd2;
					} else {
						codeIndex = USBMidiCinSysExEnd1;
					}
				}

				USBMidiPacket packet = USBMidiPacket::Make(cable, codeIndex, 0, 0, 0);
				for (std::size_t index = 0; index < count; index++) {
					packet.bytes[1 + index] = bytes[offset + index];
				}

				// The room was counted above in this section, so the push cannot fail.
				txRing.Push(packet);
				offset += count;
			}
		}

		System::CriticalSection(false);
		return status;
	}


	// Takes the oldest received event. Returns notReady when the RX queue is empty or the function is not configured. The peek,
	// the mark and the commit share one section, so a reset cannot clear the queue between them.
	ResultStatus ReadEvent(USBMidiPacket& packet) {
		ResultStatus status = ResultStatus::notReady;
		System::CriticalSection(true);
		if (configured && rxRing.PeekMultiple(&packet, 1) == 1 && rxRing.MarkRead(1) == ResultStatus::ok) {
			rxRing.CommitReads();
			status = ResultStatus::ok;
		}

		System::CriticalSection(false);
		return status;
	}


	// Queued events waiting to be sent, and received events waiting for ReadEvent(). Both are 0 while the function is not configured.
	uint16 GetSendableCount() const {
		if (!configured) {
			return 0;
		}

		return static_cast<uint16>(txRing.Size());
	}


	uint16 GetReadableCount() const {
		if (!configured) {
			return 0;
		}

		return rxRing.Size();
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		if (GetInterfaces().empty()) {
			return ResultStatus::invalidParameter;
		}

		bool hasIn = false;
		bool hasOut = false;
		uint8 newInAddress = 0;
		uint8 newOutAddress = 0;
		for (uint8 address : GetEndpoints()) {
			if (!USBEndpoint::IsValid(address) || USBEndpoint::Number(address) == 0) {
				return ResultStatus::invalidParameter;
			}

			if (USBEndpoint::IsIn(address)) {
				if (hasIn) {
					return ResultStatus::invalidParameter;
				}

				hasIn = true;
				newInAddress = address;
			} else {
				if (hasOut) {
					return ResultStatus::invalidParameter;
				}

				hasOut = true;
				newOutAddress = address;
			}
		}

		if (!hasIn || !hasOut) {
			return ResultStatus::invalidParameter;
		}

		configured = false;
		inFlight = false;
		outArmed = false;
		USBDeviceAdapter::EndpointConfig inConfig;
		inConfig.address = newInAddress;
		inConfig.type = USBEndpointType::Bulk;
		inConfig.maxPacketSize = PacketSize;
		inConfig.doubleBuffer = false;
		ResultStatus status = context.OpenEndpoint(inConfig);
		if (status != ResultStatus::ok) {
			return status;
		}

		USBDeviceAdapter::EndpointConfig outConfig = inConfig;
		outConfig.address = newOutAddress;
		status = context.OpenEndpoint(outConfig);
		if (status != ResultStatus::ok) {
			context.CloseEndpoint(newInAddress);
			return status;
		}

		inAddress = newInAddress;
		outAddress = newOutAddress;
		inHalted = false;
		outHalted = false;
		// A configuration starts with empty queues.
		txRing.Clear();
		rxRing.Clear();
		sessionGeneration++;
		configured = true;
		TryArmOut(context);
		return ResultStatus::ok;
	}


	// The queues are kept. ReadEvent() and the counts refuse while unconfigured, and the next configuration or bus reset clears
	// the queues.
	void OnDeconfigured() override {
		configured = false;
		inFlight = false;
		outArmed = false;
		sessionGeneration++;
	}


	void OnBusReset() override {
		configured = false;
		inFlight = false;
		inHalted = false;
		outArmed = false;
		outHalted = false;
		txRing.Clear();
		rxRing.Clear();
		sessionGeneration++;
	}


	void OnTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) override {
		if (!configured) {
			return;
		}

		if (address == inAddress) {
			if (!inFlight) {
				return;
			}

			inFlight = false;
			inTransfers++;
			txRing.CommitReads();
			TryStartIn(context);
			return;
		}

		if (address == outAddress) {
			if (!outArmed) {
				return;
			}

			outArmed = false;
			std::size_t stored = static_cast<std::size_t>(byteCount);
			if (stored > outBuffer.size()) {
				stored = outBuffer.size();
			}

			ProcessReceived(std::span<const uint8>(outBuffer).first(stored));
			TryArmOut(context);
		}
	}


	void OnEndpointHaltChanged(uint8 address, bool halted, USBDeviceContext& context) override {
		if (!configured) {
			return;
		}

		if (address == inAddress) {
			inHalted = halted;
			if (!halted) {
				TryStartIn(context);
				return;
			}

			// Stall() abandoned the transfer without an event, so its packets go back to the queue. While TryStartIn() holds its
			// claim, that call sees inHalted and returns the packets itself.
			if (!txClaimed && inFlight) {
				inFlight = false;
				txRing.RollbackReads();
			}

			return;
		}

		if (address == outAddress) {
			outHalted = halted;
			if (halted) {
				outArmed = false;
			} else {
				TryArmOut(context);
			}
		}
	}


	void Process(USBDeviceContext& context) override {
		ProcessOut(context);
		TryStartIn(context);
	}


private:
	// Checks that the packet is one complete MIDI event for its code index: the status byte agrees with the code index, the data
	// bytes are 7-bit, and the bytes the code index does not use are zero. The SysEx code indexes 0x4, 0x6 and 0x7 are refused,
	// because their framing belongs to SendSysEx().
	static bool IsValidEvent(const USBMidiPacket& packet) {
		uint8 codeIndex = packet.CodeIndex();
		uint8 status = packet.bytes[1];
		uint8 data1 = packet.bytes[2];
		uint8 data2 = packet.bytes[3];

		if (codeIndex >= USBMidiCinNoteOff && codeIndex <= USBMidiCinPitchBend) {
			// The high nibble of the status is the code index. Program change and channel pressure carry one data byte.
			bool oneDataByte = codeIndex == USBMidiCinProgramChange || codeIndex == USBMidiCinChannelPressure;
			return (status >> 4) == codeIndex && data1 < 0x80 && (oneDataByte ? data2 == 0 : data2 < 0x80);
		}

		// System Common and Single Byte packets: the two-byte common messages (0xF1, 0xF3), the three-byte song position
		// (0xF2), the single-byte tune request (0xF6) and the real-time messages (0xF8 and up).
		if (codeIndex == 0x2) {
			return (status == 0xF1 || status == 0xF3) && data1 < 0x80 && data2 == 0;
		}

		if (codeIndex == 0x3) {
			return status == 0xF2 && data1 < 0x80 && data2 < 0x80;
		}

		if (codeIndex == USBMidiCinSysExEnd1) {
			return status == 0xF6 && data1 == 0 && data2 == 0;
		}

		if (codeIndex == 0xF) {
			return status >= 0xF8 && data1 == 0 && data2 == 0;
		}

		return false;
	}


	// Common path of the channel messages. The data bytes must be 7-bit, and the second data byte is zero for the two-byte messages.
	ResultStatus SendChannel(uint8 cable, uint8 codeIndex, uint8 status, uint8 channel, uint8 data1, uint8 data2, bool threeBytes) {
		if (channel > 15 || data1 > 127 || data2 > 127) {
			return ResultStatus::invalidParameter;
		}

		uint8 secondByte = threeBytes ? data2 : 0;
		return SendEvent(USBMidiPacket::Make(cable, codeIndex, static_cast<uint8>(status | channel), data1, secondByte));
	}


	// Copies the packets of a received transfer into the RX queue and gives their SysEx bytes to the SysEx handler. Only called
	// with room for the whole transfer (see CanAcceptOut()), so the pushes cannot fail.
	void ProcessReceived(std::span<const uint8> received) {
		for (std::size_t offset = 0; offset + 4 <= received.size(); offset += 4) {
			USBMidiPacket packet;
			for (std::size_t index = 0; index < 4; index++) {
				packet.bytes[index] = received[offset + index];
			}

			uint8 codeIndex = packet.CodeIndex();
			// Code index 0x0 and 0x1 are reserved and carry no event.
			if (codeIndex < 0x2) {
				continue;
			}

			// The function has no queue for a cable it does not have, so the packet is dropped.
			if (packet.Cable() >= Cables) {
				continue;
			}

			rxRing.Push(packet);
			if (eventHandler) {
				eventHandler(packet);
			}

			// A 0x5 packet is SysEx only when its byte is data or the F7 that ends the message. 0xF6 is a single-byte System Common
			// message, not SysEx data.
			bool sysExPacket = codeIndex == USBMidiCinSysExStart || codeIndex == USBMidiCinSysExEnd2 || codeIndex == USBMidiCinSysExEnd3
				|| (codeIndex == USBMidiCinSysExEnd1 && (packet.bytes[1] < 0xF0 || packet.bytes[1] == 0xF7));
			if (sysExPacket && sysExHandler) {
				// 0x4 and 0x7 carry three bytes, 0x5 one and 0x6 two.
				std::size_t count = (codeIndex == USBMidiCinSysExStart || codeIndex == USBMidiCinSysExEnd3) ? 3 : codeIndex - USBMidiCinSysExEnd1 + 1;
				sysExHandler(packet.Cable(), std::span<const uint8>(packet.bytes).subspan(1, count));
			}
		}
	}


	bool CanAcceptOut() const {
		return rxRing.GetFreeSpace() >= PacketsPerTransfer;
	}


	// Arms the OUT endpoint when the RX queue has room for a whole transfer. It runs in interrupt context or inside a critical
	// section, as StartOutTransfer() requires. A synchronous completion inside StartOutTransfer() re-arms from its own callback,
	// which the recursion allows.
	void TryArmOut(USBDeviceContext& context) {
		if (!configured || outArmed || outHalted || !CanAcceptOut()) {
			return;
		}

		outArmed = true;
		uint32 startsBefore = outStarts;
		outStarts++;
		ResultStatus status = context.StartOutTransfer(outAddress, outBuffer);
		// A start inside the call bumped the count, so the flag now belongs to that start and this failure must not clear it.
		bool nestedStart = outStarts != startsBefore + 1;
		if (status != ResultStatus::ok && !nestedStart) {
			outArmed = false;
		}
	}


	// Thread side. The arm is claimed and started inside one section, as StartOutTransfer() requires. Sections nest, so the
	// synchronous completion may store its packets through the RX queue from here.
	void ProcessOut(USBDeviceContext& context) {
		System::CriticalSection(true);
		TryArmOut(context);
		System::CriticalSection(false);
	}


	// Starts the next IN transfer when the endpoint is idle and the queue holds packets. The claim (inFlight and txClaimed) is taken
	// in a section, the packets are marked outside it, and the start runs in the section that checks the session and the halt
	// state. The queue keeps the marked packets until the completion commits them, or a halt or a failed start returns them.
	void TryStartIn(USBDeviceContext& context) {
		System::CriticalSection(true);
		bool claim = configured && !inFlight && !inHalted && !txClaimed;
		if (claim) {
			inFlight = true;
			txClaimed = true;
		}

		uint32 generation = sessionGeneration;
		System::CriticalSection(false);
		if (!claim) {
			return;
		}

		bool zeroLength = false;
		uint16 count = BuildInTransfer(zeroLength);

		System::CriticalSection(true);
		txClaimed = false;
		// A bus reset or a new configuration between the build and this section clears the queue, so the packets BuildInTransfer()
		// copied belong to the old session. The generation catches that even when the function is configured again, where the
		// configured and halt checks would pass. Without it, the stale packets would go out in the new session.
		bool sameSession = sessionGeneration == generation;
		bool send = count > 0 && configured && sameSession && !inHalted;
		uint32 changesBefore = inTransfers;
		ResultStatus status = ResultStatus::notReady;
		if (send) {
			inTransfers++;
			status = context.StartInTransfer(inAddress, std::span<const uint8>(inBuffer).first(count), zeroLength);
		}

		// A transfer that did not start gives its packets back in the same section that releases the claim, so no interrupt sees
		// the claim released while the marks are still set. A reset cleared the queue and its marks, so only a same-session
		// abandon has marks to give back. A completion or a start inside the call (not possible under the adapter contract, which
		// never completes IN inside the call) has already committed or taken the marks, so this failure leaves them alone.
		bool started = send && status == ResultStatus::ok;
		bool nestedStart = send && inTransfers != changesBefore + 1;
		if (!started && !nestedStart) {
			inFlight = false;
			if (count > 0 && sameSession) {
				txRing.RollbackReads();
			}
		}

		System::CriticalSection(false);
	}


	// Marks up to 16 queued packets for the next transfer and copies them into the IN buffer. Returns the byte count, 0 when
	// the queue is empty. Nothing is marked while a transfer is in flight, so the peek starts at the head. A transfer of 16
	// packets is a full packet of 64 bytes: it ends with a zero-length packet when no event is queued behind it, because the
	// host's read would otherwise wait for the next one. A transfer with events behind it needs none, the next one starts at its
	// completion. A shorter transfer ends with a short packet already. While the claim is held SendEvent() can only add events
	// (a bus reset removes them, and the session check in TryStartIn() drops the transfer then), so an event that arrives after
	// this check only makes the zero-length packet unneeded: the event goes out in the next transfer.
	uint16 BuildInTransfer(bool& zeroLength) {
		zeroLength = false;
		uint16 available = txRing.Size();
		uint16 wanted = available < PacketsPerTransfer ? available : PacketsPerTransfer;
		if (wanted == 0) {
			return 0;
		}

		uint16 got = txRing.PeekMultiple(txPackets.data(), wanted);
		if (got == 0) {
			return 0;
		}

		if (txRing.MarkRead(got) != ResultStatus::ok) {
			return 0;
		}

		for (uint16 index = 0; index < got; index++) {
			for (uint16 byte = 0; byte < 4; byte++) {
				inBuffer[index * 4 + byte] = txPackets[index].bytes[byte];
			}
		}

		zeroLength = got == PacketsPerTransfer && txRing.Size() == got;
		return static_cast<uint16>(got * 4);
	}
};
