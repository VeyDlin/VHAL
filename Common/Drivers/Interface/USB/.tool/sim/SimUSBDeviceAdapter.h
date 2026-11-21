#pragma once
#include <VHAL.h>
#include <Adapter/USBDeviceAdapter.h>
#include <Adapter/Utilities/USBTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>


// Software controller for the host tests on the PC. It implements the USBDeviceAdapter contract and enforces its rules
// (see that header), so the core is tested against a controller that behaves like the hardware does. There is no USB
// timing: each injection or host transaction raises the events it causes before it returns, as an interrupt would. The one
// timing rule that is modelled is the isochronous deferral of an IN packet (see IsoInFrame()).
//
// The limits are those of the G431 fsdev: 8 endpoint numbers and 1 KB of packet memory.
class SimUSBDeviceAdapter : public USBDeviceAdapter {
public:
	static constexpr uint8 HardwareEndpointCount = 8;
	static constexpr uint32 PacketMemorySize = 1024;
	// The port takes the buffer table (8 bytes per endpoint register, 64 bytes on G4) out of packet memory when it is initialized, and
	// endpoint 0 takes two 64-byte blocks above it. The endpoints the core opens are placed above those, block by block, as the port places
	// them (FindBlock()).
	static constexpr uint32 BufferTableSize = 64;
	static constexpr uint16 FullSpeedMaxPacketSize = 64;
	static constexpr uint16 MaxIsochronousPacketSize = 1023;


	// The last completed endpoint 0 transfer, as the core received it.
	struct ControlCompletion {
		uint8 address = 0;
		uint32 byteCount = 0;
	};


private:
	// One direction of one endpoint number. Endpoint 0 has two slots (OUT and IN) that share one halt.
	struct Slot {
		bool open = false;
		bool halted = false;
		// The DATA0 (false) or DATA1 (true) PID of the next packet on this direction.
		bool toggle = false;
		bool armed = false;
		bool sendZeroLengthPacket = false;
		// Accepted Stall() calls for this address. Kept across bus resets and closes, so a test can count them.
		uint32 stallCount = 0;
		// Isochronous only: the buffer the next packet uses. It flips in every frame, armed or not, as DTOG does on the hardware. Reset with the endpoint.
		bool isoBufferSelect = false;
		// Isochronous only: frames in which the host found no transfer armed (see GetMissedFrames()).
		uint32 missedFrames = 0;
		// Isochronous IN only: the armed packet is not in the buffer yet. It is set when the packet is armed from outside the endpoint's own
		// completion callback, and the next token clears it: that token sends a zero-length packet and the packet is written (see IsoInFrame()).
		bool pending = false;
		// The endpoint's completion callback is running, so a packet armed now is written at once and is not pending.
		bool inCompletion = false;
		// The packet memory blocks of the endpoint: one for a single-buffered endpoint, two for a double-buffered or an isochronous one (ADDR_TX
		// and ADDR_RX on the port). Each block is blockSize bytes at the address in blockAddress.
		std::array<uint32, 2> blockAddress{};
		uint32 blockSize = 0;
		uint32 blockCount = 0;
		// Bytes moved so far in the armed transfer.
		uint32 position = 0;
		USBDeviceAdapter::EndpointConfig config;
		// IN: the span being sent. OUT: the buffer being filled. Empty while nothing is armed.
		std::span<const uint8> source;
		std::span<uint8> target;
	};


	// Index is number * 2 + (IN ? 1 : 0), so endpoint 0 is index 0 (OUT) and index 1 (IN).
	std::array<Slot, HardwareEndpointCount * 2> slots;
	bool initialized = false;
	bool connected = false;
	bool suspended = false;
	// Set by RemoteWakeup() until FinishResume() ends the signalling and raises onResume.
	bool resumeSignalling = false;
	bool hasPendingAddress = false;
	uint8 deviceAddress = 0;
	uint8 pendingAddress = 0;
	uint16 frameNumber = 0;
	ControlCompletion lastControlCompletion;
	uint32 controlCompletionCount = 0;
	// Endpoint 0 stages that reached the host with DATA0 instead of DATA1 (see CountStageStart()).
	uint32 toggleMismatchCount = 0;


public:
	// Injections: the bus or the host does something, and the events it causes run before the call returns.
	void InjectBusReset(USBSpeed speed = USBSpeed::Full) {
		for (Slot& slot : slots) {
			CloseSlot(slot);
		}

		deviceAddress = 0;
		hasPendingAddress = false;
		suspended = false;
		resumeSignalling = false;
		OpenControlSlot(USBEndpoint::ControlOut);
		OpenControlSlot(USBEndpoint::ControlIn);
		CallBusReset(speed);
	}


	void InjectSetup(const USBSetupBytes& setup) {
		// The adapter abandons the control pipe with no completion event, and a SETUP clears its halt and any address that
		// SET_ADDRESS has not yet applied.
		AbandonTransfer(USBEndpoint::ControlOut);
		AbandonTransfer(USBEndpoint::ControlIn);
		SetControlHalt(false);
		hasPendingAddress = false;

		// The SETUP packet itself is DATA0 (USB 2.0 section 8.6.2). The stage after it starts at DATA1, which
		// StartInTransfer() and StartOutTransfer() set on endpoint 0.
		slots[SlotIndex(USBEndpoint::ControlOut)].toggle = false;
		slots[SlotIndex(USBEndpoint::ControlIn)].toggle = false;
		CallSetup(setup);
	}


	void InjectSuspend() {
		suspended = true;
		CallSuspend();
	}


	void InjectResume() {
		suspended = false;
		CallResume();
	}


	// Ends the resume signalling that RemoteWakeup() started and raises onResume, as the port does once the signalling is over.
	// Returns notReady when no signalling is in progress.
	ResultStatus FinishResume() {
		if (!resumeSignalling) {
			return ResultStatus::notReady;
		}

		resumeSignalling = false;
		CallResume();
		return ResultStatus::ok;
	}


	// The frame number is kept whether or not the events are on, because the hardware counts frames either way.
	void InjectStartOfFrame(uint16 frame) {
		frameNumber = static_cast<uint16>(frame & 0x07FF);

		if (parameters.startOfFrameEvents) {
			CallStartOfFrame(frameNumber);
		}
	}


	void InjectError(Error error) {
		CallError(error);
	}


	// The host sends an OUT packet sequence to the armed transfer on address. The packets go into the transfer until it
	// completes, and the rest of data is not sent: the host sends it again into the next armed transfer. Returns the
	// bytes stored in the transfer. A host with nothing armed gets NAK (notReady), and a halted endpoint gets STALL.
	Result<uint32> HostOut(uint8 address, std::span<const uint8> data) {
		if (USBEndpoint::IsIn(address)) {
			return ResultStatus::invalidParameter;
		}

		Slot* slot = FindSlot(address);

		if (slot == nullptr || !slot->open) {
			return ResultStatus::notExist;
		}

		// An isochronous endpoint moves one packet per frame: IsoOutFrame() is its host call.
		if (slot->config.type == USBEndpointType::Isochronous) {
			return ResultStatus::invalidParameter;
		}

		if (slot->halted) {
			return ResultStatus::accessError;
		}

		if (!connected || !slot->armed) {
			return ResultStatus::notReady;
		}

		CountStageStart(*slot, address);
		uint32 stored = slot->position;

		// A zero-length packet is a packet of its own: it is what ends a status stage, and it completes an empty buffer.
		if (data.empty()) {
			ReceivePacket(address, data, stored);
			return stored;
		}

		std::size_t maxPacketSize = slot->config.maxPacketSize;
		std::size_t offset = 0;

		while (offset < data.size()) {
			std::size_t chunk = std::min(maxPacketSize, data.size() - offset);

			if (ReceivePacket(address, data.subspan(offset, chunk), stored)) {
				return stored;
			}

			offset += chunk;
		}

		return stored;
	}


	// The host takes the whole armed IN transfer, packet by packet, and copies it to out from its start. received is set to
	// the data bytes copied, which is the transfer's size (a zero-length packet adds none). The transfer completes with its
	// last packet: a short or zero-length packet, or the last full packet when the data ends on a packet boundary (with the
	// zero-length packet flag, that packet follows the last full one). out must hold the whole transfer. When it does not,
	// nothing is taken, the transfer stays armed, and the call returns invalidParameter. Returns notReady (NAK) or
	// accessError (STALL) as HostOut does.
	ResultStatus HostIn(uint8 address, std::span<uint8> out, uint32& received) {
		received = 0;

		if (!USBEndpoint::IsIn(address)) {
			return ResultStatus::invalidParameter;
		}

		Slot* slot = FindSlot(address);

		if (slot == nullptr || !slot->open) {
			return ResultStatus::notExist;
		}

		// An isochronous endpoint moves one packet per frame: IsoInFrame() is its host call.
		if (slot->config.type == USBEndpointType::Isochronous) {
			return ResultStatus::invalidParameter;
		}

		if (slot->halted) {
			return ResultStatus::accessError;
		}

		if (!connected || !slot->armed) {
			return ResultStatus::notReady;
		}

		std::span<const uint8> data = slot->source;

		if (data.size() > out.size()) {
			return ResultStatus::invalidParameter;
		}

		bool sendZeroLengthPacket = slot->sendZeroLengthPacket;
		std::size_t maxPacketSize = slot->config.maxPacketSize;
		CountStageStart(*slot, address);

		// An empty span is the status stage of an IN request: exactly one zero-length packet, whatever the flag says.
		if (data.empty()) {
			slot->toggle = !slot->toggle;
			FinishTransfer(address, 0);
			return ResultStatus::ok;
		}

		std::copy(data.begin(), data.end(), out.begin());
		received = static_cast<uint32>(data.size());

		std::size_t offset = 0;

		while (offset < data.size()) {
			std::size_t chunk = std::min(maxPacketSize, data.size() - offset);
			offset += chunk;
			slot->toggle = !slot->toggle;

			if (chunk < maxPacketSize) {
				break;
			}

			if (offset == data.size()) {
				if (sendZeroLengthPacket) {
					slot->toggle = !slot->toggle;
				}

				break;
			}
		}

		FinishTransfer(address, received);
		return ResultStatus::ok;
	}


	// One frame of the host's IN token on an isochronous endpoint. The packet armed for the frame is copied to out, and length is its size.
	// A frame with no packet armed is answered with a zero-length packet: length is 0, nothing completes and no event is raised, and the
	// frame is counted in GetMissedFrames(). Every frame flips the buffer select, armed or not, as the hardware's DTOG does. invalidParameter
	// for an endpoint that is not isochronous, or when out cannot hold the packet (then nothing is taken and nothing moves).
	//
	// A packet armed from outside its endpoint's completion callback is pending, as the port's txDeferred is (USBDeviceAdapter.h, "Isochronous
	// endpoints"): the first token after the arm sends a zero-length packet with no completion, and writes the packet, and the next token sends
	// it. A pending token is not a missed frame, because a transfer is armed; it copies nothing into out, so out needs no room for the packet.
	ResultStatus IsoInFrame(uint8 address, std::span<uint8> out, uint32& length) {
		length = 0;
		if (!USBEndpoint::IsIn(address)) {
			return ResultStatus::invalidParameter;
		}

		Slot* slot = FindSlot(address);

		if (slot == nullptr || !slot->open) {
			return ResultStatus::notExist;
		}

		if (slot->config.type != USBEndpointType::Isochronous) {
			return ResultStatus::invalidParameter;
		}

		if (!connected) {
			return ResultStatus::notReady;
		}

		// The pending packet is not in the buffer, so this token finds it empty and sends a zero-length packet. Nothing completes; the packet is
		// written for the next token.
		if (slot->armed && slot->pending) {
			slot->pending = false;
			slot->isoBufferSelect = !slot->isoBufferSelect;
			return ResultStatus::ok;
		}

		std::span<const uint8> data = slot->armed ? slot->source : std::span<const uint8>();

		if (data.size() > out.size()) {
			return ResultStatus::invalidParameter;
		}

		std::copy(data.begin(), data.end(), out.begin());
		length = static_cast<uint32>(data.size());
		slot->isoBufferSelect = !slot->isoBufferSelect;

		if (!slot->armed) {
			slot->missedFrames++;
			return ResultStatus::ok;
		}

		FinishTransfer(address, length);
		return ResultStatus::ok;
	}


	// One frame of the host's OUT token on an isochronous endpoint, carrying data. The frame is stored whatever is armed, and the buffer select
	// flips in every frame. With a transfer armed the packet completes it with its byte count; a packet that does not fit is dropped whole, with
	// DataOverrun, and the transfer completes with 0. With no transfer armed the packet is dropped with no event, and the frame is counted in
	// GetMissedFrames(). The call returns ok when the endpoint took the frame (stored or dropped by overrun) and notReady when the packet was
	// dropped for want of an armed transfer, so a test sees both.
	ResultStatus IsoOutFrame(uint8 address, std::span<const uint8> data) {
		if (USBEndpoint::IsIn(address)) {
			return ResultStatus::invalidParameter;
		}

		Slot* slot = FindSlot(address);

		if (slot == nullptr || !slot->open) {
			return ResultStatus::notExist;
		}

		if (slot->config.type != USBEndpointType::Isochronous) {
			return ResultStatus::invalidParameter;
		}

		if (!connected) {
			return ResultStatus::notReady;
		}

		slot->isoBufferSelect = !slot->isoBufferSelect;

		if (!slot->armed) {
			slot->missedFrames++;
			return ResultStatus::notReady;
		}

		uint32 room = static_cast<uint32>(slot->target.size());

		if (data.size() > room) {
			CallError(Error::DataOverrun);
			FinishTransfer(address, 0);
			return ResultStatus::ok;
		}

		std::copy(data.begin(), data.end(), slot->target.begin());
		FinishTransfer(address, static_cast<uint32>(data.size()));
		return ResultStatus::ok;
	}


	// Frames the host found no transfer armed for, on an isochronous endpoint: an IN frame answered with a zero-length packet, or an OUT packet
	// dropped. Zero for any other endpoint.
	uint32 GetMissedFrames(uint8 address) const {
		const Slot* slot = FindSlot(address);
		return slot != nullptr ? slot->missedFrames : 0;
	}


	// The buffer the next isochronous packet uses. Every frame flips it, as the hardware's DTOG does, so it is not a PID.
	bool GetIsoBufferSelect(uint8 address) const {
		const Slot* slot = FindSlot(address);
		return slot != nullptr && slot->isoBufferSelect;
	}


	uint8 GetAddress() const {
		return deviceAddress;
	}


	const EndpointConfig* FindEndpoint(uint8 address) const {
		const Slot* slot = FindSlot(address);

		if (slot == nullptr || !slot->open) {
			return nullptr;
		}

		return &slot->config;
	}


	bool HasArmedTransfer(uint8 address) const {
		const Slot* slot = FindSlot(address);
		return slot != nullptr && slot->armed;
	}


	uint32 GetStallCount(uint8 address) const {
		const Slot* slot = FindSlot(address);
		return slot != nullptr ? slot->stallCount : 0;
	}


	// The toggle of the next packet on this direction. Reset to DATA0 by opening, closing, bus reset, SETUP and ClearStall().
	bool GetDataToggle(uint8 address) const {
		const Slot* slot = FindSlot(address);
		return slot != nullptr && slot->toggle;
	}


	// True when the endpoint is open and has two buffers: a double-buffered bulk endpoint, or an isochronous one (which has no flag of
	// its own). A request can be refused with notSupported, so a test checks what the port granted and not only what it was asked for.
	bool IsDoubleBuffered(uint8 address) const {
		const Slot* slot = FindSlot(address);
		return slot != nullptr && slot->open && HasTwoBuffers(slot->config);
	}


	// Endpoint 0 completions the core has received, and the last one. A transfer is counted when it completes, so a request
	// that ended with its status stage has a completion of zero bytes on the status address.
	uint32 GetControlCompletionCount() const {
		return controlCompletionCount;
	}


	ControlCompletion GetLastControlCompletion() const {
		return lastControlCompletion;
	}


	// Endpoint 0 stages that started with DATA0. Zero when every stage starts at DATA1, as USB 2.0 section 8.6.2 requires.
	uint32 GetToggleMismatchCount() const {
		return toggleMismatchCount;
	}


	ResultStatus Connect() override {
		if (!initialized) {
			return ResultStatus::notReady;
		}

		connected = true;
		return ResultStatus::ok;
	}


	// Abandons the transfers in flight without events, as the contract says. Endpoints stay open for the core to close.
	ResultStatus Disconnect() override {
		connected = false;

		for (Slot& slot : slots) {
			AbandonSlot(slot);
		}

		return ResultStatus::ok;
	}


	bool IsConnected() const override {
		return connected;
	}


	ResultStatus SetAddress(uint8 address) override {
		if (address > 127) {
			return ResultStatus::invalidParameter;
		}

		pendingAddress = address;
		hasPendingAddress = true;
		return ResultStatus::ok;
	}


	ResultStatus OpenEndpoint(const EndpointConfig& config) override {
		if (!initialized) {
			return ResultStatus::notReady;
		}

		if ((config.address & 0x70) != 0 || USBEndpoint::IsControl(config.address)) {
			return ResultStatus::invalidParameter;
		}

		if (USBEndpoint::Number(config.address) >= parameters.endpointCount) {
			return ResultStatus::invalidParameter;
		}

		if (config.type == USBEndpointType::Control) {
			return ResultStatus::invalidParameter;
		}

		if (config.type == USBEndpointType::Isochronous && (config.maxPacketSize == 0 || config.maxPacketSize > MaxIsochronousPacketSize)) {
			return ResultStatus::invalidParameter;
		}

		if (config.type == USBEndpointType::Bulk && !IsBulkPacketSize(config.maxPacketSize)) {
			return ResultStatus::invalidParameter;
		}

		if (config.type == USBEndpointType::Interrupt && (config.maxPacketSize == 0 || config.maxPacketSize > FullSpeedMaxPacketSize)) {
			return ResultStatus::invalidParameter;
		}

		if (config.doubleBuffer && config.type != USBEndpointType::Bulk) {
			return ResultStatus::invalidParameter;
		}

		Slot& slot = slots[SlotIndex(config.address)];

		if (slot.armed) {
			return ResultStatus::busy;
		}

		// fsdev shares one endpoint register between the two directions of a number. Its type bits are shared, and a
		// double-buffered endpoint uses both directions, so the other direction limits this one. The port checks this
		// before packet memory, and so does the simulator.
		uint8 number = USBEndpoint::Number(config.address);
		uint8 otherAddress = USBEndpoint::IsIn(config.address) ? USBEndpoint::MakeOut(number) : USBEndpoint::MakeIn(number);
		const Slot& other = slots[SlotIndex(otherAddress)];

		if (other.open) {
			if (other.config.type != config.type) {
				return ResultStatus::invalidParameter;
			}

			// The register cannot hold double buffering next to the other direction, so the port refuses it as a capability.
			if (HasTwoBuffers(config)) {
				return ResultStatus::notSupported;
			}

			// A single-buffered request beside a double-buffered other direction is a conflict the caller created.
			if (HasTwoBuffers(other.config)) {
				return ResultStatus::invalidParameter;
			}
		}

		// The blocks are looked for before the slot gives up its old ones, and the slot is skipped while they are, as the port zeroes its sizes
		// first. A refused call changes nothing.
		uint32 blockSize = BlockSizeOf(config.maxPacketSize);
		uint32 blockCount = HasTwoBuffers(config) ? 2U : 1U;
		uint32 first = 0;
		uint32 second = 0;
		bool fits = FindBlock(blockSize, 0, 0, slot, first);
		if (fits && blockCount == 2U) {
			fits = FindBlock(blockSize, first, blockSize, slot, second);
		}

		if (!fits) {
			return ResultStatus::outOfMemory;
		}

		CloseSlot(slot);
		slot.open = true;
		slot.config = config;
		slot.blockSize = blockSize;
		slot.blockCount = blockCount;
		slot.blockAddress[0] = first;
		slot.blockAddress[1] = second;
		return ResultStatus::ok;
	}


	ResultStatus CloseEndpoint(uint8 address) override {
		if (USBEndpoint::IsControl(address)) {
			return ResultStatus::invalidParameter;
		}

		Slot* slot = FindSlot(address);

		if (slot == nullptr || !slot->open) {
			return ResultStatus::notExist;
		}

		CloseSlot(*slot);
		return ResultStatus::ok;
	}


	void CloseAllEndpoints() override {
		// Slots 0 and 1 are endpoint 0, which the adapter keeps open.
		for (std::size_t index = 2; index < slots.size(); index++) {
			CloseSlot(slots[index]);
		}
	}


	ResultStatus StartInTransfer(uint8 address, std::span<const uint8> data, bool sendZeroLengthPacket) override {
		if (!USBEndpoint::IsIn(address)) {
			return ResultStatus::invalidParameter;
		}

		if (!connected) {
			return ResultStatus::notReady;
		}

		Slot* slot = FindSlot(address);

		if (slot == nullptr || !slot->open) {
			return ResultStatus::notExist;
		}

		if (slot->armed) {
			return ResultStatus::busy;
		}

		// An isochronous transfer is one packet, so its data fits one packet of the endpoint. An empty span is a zero-length packet.
		if (slot->config.type == USBEndpointType::Isochronous && data.size() > slot->config.maxPacketSize) {
			return ResultStatus::invalidParameter;
		}

		slot->armed = true;
		slot->source = data;
		slot->target = {};
		slot->sendZeroLengthPacket = sendZeroLengthPacket;
		slot->position = 0;
		// An isochronous IN packet armed from its own completion callback goes out at the next token. Armed from anywhere else it is pending
		// (see IsoInFrame()).
		slot->pending = slot->config.type == USBEndpointType::Isochronous && !slot->inCompletion;
		StartControlStage(*slot, address);
		return ResultStatus::ok;
	}


	ResultStatus StartOutTransfer(uint8 address, std::span<uint8> buffer) override {
		if (USBEndpoint::IsIn(address)) {
			return ResultStatus::invalidParameter;
		}

		if (!connected) {
			return ResultStatus::notReady;
		}

		Slot* slot = FindSlot(address);

		if (slot == nullptr || !slot->open) {
			return ResultStatus::notExist;
		}

		if (slot->armed) {
			return ResultStatus::busy;
		}

		slot->armed = true;
		slot->source = {};
		slot->target = buffer;
		slot->sendZeroLengthPacket = false;
		slot->position = 0;
		StartControlStage(*slot, address);
		return ResultStatus::ok;
	}


	ResultStatus Stall(uint8 address) override {
		Slot* slot = FindSlot(address);

		if (slot == nullptr || !slot->open) {
			return ResultStatus::notExist;
		}

		// An isochronous endpoint has no halt, so the request is refused and nothing is counted or abandoned.
		if (slot->config.type == USBEndpointType::Isochronous) {
			return ResultStatus::invalidParameter;
		}

		slot->stallCount++;

		if (USBEndpoint::IsControl(address)) {
			SetControlHalt(true);
			AbandonTransfer(USBEndpoint::ControlOut);
			AbandonTransfer(USBEndpoint::ControlIn);
		} else {
			slot->halted = true;
			AbandonSlot(*slot);
		}

		return ResultStatus::ok;
	}


	// The toggle goes back to DATA0 on both directions of endpoint 0, because they share the one halt.
	ResultStatus ClearStall(uint8 address) override {
		Slot* slot = FindSlot(address);

		if (slot == nullptr || !slot->open) {
			return ResultStatus::notExist;
		}

		// An isochronous endpoint is never halted and has no toggle to reset, so the request is acknowledged and changes nothing.
		if (slot->config.type == USBEndpointType::Isochronous) {
			return ResultStatus::ok;
		}

		if (USBEndpoint::IsControl(address)) {
			SetControlHalt(false);
			slots[SlotIndex(USBEndpoint::ControlOut)].toggle = false;
			slots[SlotIndex(USBEndpoint::ControlIn)].toggle = false;
		} else {
			slot->halted = false;
			slot->toggle = false;
		}

		return ResultStatus::ok;
	}


	bool IsStalled(uint8 address) const override {
		const Slot* slot = FindSlot(address);
		return slot != nullptr && slot->halted;
	}


	// Starts the resume signalling and returns at once, as the contract says. The port ends the signalling later and raises
	// onResume then, which FinishResume() injects. Until that runs the core is still Suspended.
	ResultStatus RemoteWakeup() override {
		if (!suspended) {
			return ResultStatus::notReady;
		}

		suspended = false;
		resumeSignalling = true;
		return ResultStatus::ok;
	}


	uint16 GetFrameNumber() const override {
		return frameNumber;
	}


	// Nothing is pending on the PC: every injection and host transaction raises its events before it returns.
	void IrqHandler() override {
	}


protected:
	ResultStatus Initialization() override {
		initialized = false;
		ReserveControlBlocks();

		ResultStatus status = BeforeInitialization();

		if (status != ResultStatus::ok) {
			return status;
		}

		if (parameters.endpointCount == 0 || parameters.endpointCount > HardwareEndpointCount) {
			return ResultStatus::invalidParameter;
		}

		status = AfterInitialization();

		if (status != ResultStatus::ok) {
			return status;
		}

		initialized = true;
		return ResultStatus::ok;
	}


private:
	static bool IsAddressValid(uint8 address) {
		return (address & 0x70) == 0 && USBEndpoint::Number(address) < HardwareEndpointCount;
	}


	static bool IsBulkPacketSize(uint16 size) {
		return size == 8 || size == 16 || size == 32 || size == FullSpeedMaxPacketSize;
	}


	// The endpoint has two buffers on the port: a double-buffered bulk endpoint, and every isochronous endpoint (the port keeps the
	// frame's buffer and the next one apart, see USBFsdev.h).
	static bool HasTwoBuffers(const EndpointConfig& config) {
		return config.doubleBuffer || config.type == USBEndpointType::Isochronous;
	}


	// The size of one packet memory block, as the port's PmaBlockSize() gives it (USBFsdev.h): the packet size rounded up to an even size, or to
	// 32 bytes above 62 bytes, so a 200-byte packet takes blocks of 224. Every endpoint type is rounded the same way.
	static uint32 BlockSizeOf(uint16 maxPacketSize) {
		uint32 size = maxPacketSize;
		return size > 62 ? (size + 31U) & ~31U : (size + 1U) & ~1U;
	}


	// The first block of size bytes above the buffer table that no block held by the slots overlaps, as the port's FindPmaBlock() finds it.
	// The candidate starts at the table and moves to the first 4-byte boundary past each block in its way. reservedSize names a block the
	// caller has found already (the first of a two-block endpoint), which counts as held. The slot being opened is skipped, because the port
	// zeroes its sizes before it looks.
	bool FindBlock(uint32 size, uint32 reservedAddress, uint32 reservedSize, const Slot& skip, uint32& address) const {
		uint32 candidate = BufferTableSize;
		while (candidate + size <= PacketMemorySize) {
			uint32 conflictEnd = 0;
			if (reservedSize != 0) {
				conflictEnd = ConflictEnd(candidate, size, reservedAddress, reservedSize);
			}

			for (std::size_t index = 0; index < slots.size() && conflictEnd == 0; index++) {
				const Slot& other = slots[index];
				if (&other == &skip) {
					continue;
				}

				for (uint32 block = 0; block < other.blockCount && conflictEnd == 0; block++) {
					conflictEnd = ConflictEnd(candidate, size, other.blockAddress[block], other.blockSize);
				}
			}

			if (conflictEnd == 0) {
				address = candidate;
				return true;
			}

			candidate = AlignUp(conflictEnd);
		}

		return false;
	}


	static uint32 ConflictEnd(uint32 start, uint32 size, uint32 blockStart, uint32 blockSize) {
		bool overlap = start < blockStart + blockSize && blockStart < start + size;
		return overlap ? blockStart + blockSize : 0;
	}


	// The port's BufferAlignment: blocks start on a 4-byte boundary.
	static uint32 AlignUp(uint32 value) {
		return (value + 3U) & ~3U;
	}


	static std::size_t SlotIndex(uint8 address) {
		return static_cast<std::size_t>(USBEndpoint::Number(address) * 2 + (USBEndpoint::IsIn(address) ? 1 : 0));
	}


	Slot* FindSlot(uint8 address) {
		if (!IsAddressValid(address)) {
			return nullptr;
		}

		return &slots[SlotIndex(address)];
	}


	const Slot* FindSlot(uint8 address) const {
		if (!IsAddressValid(address)) {
			return nullptr;
		}

		return &slots[SlotIndex(address)];
	}


	void OpenControlSlot(uint8 address) {
		Slot& slot = slots[SlotIndex(address)];
		slot.open = true;
		slot.config.address = address;
		slot.config.type = USBEndpointType::Control;
		slot.config.maxPacketSize = ControlMaxPacketSize;
		slot.config.doubleBuffer = false;
		// The bus reset closed the blocks with the slots, so they are put back here.
		ReserveControlBlocks();
	}


	// Endpoint 0 has its two blocks above the buffer table from initialization on, as the port places them: IN takes the first and OUT the
	// second. The slots keep them whether or not the control pipe is open.
	void ReserveControlBlocks() {
		Slot& in = slots[SlotIndex(USBEndpoint::ControlIn)];
		in.blockAddress[0] = BufferTableSize;
		in.blockSize = ControlMaxPacketSize;
		in.blockCount = 1;
		Slot& out = slots[SlotIndex(USBEndpoint::ControlOut)];
		out.blockAddress[0] = BufferTableSize + ControlMaxPacketSize;
		out.blockSize = ControlMaxPacketSize;
		out.blockCount = 1;
	}


	// Closes a slot and gives its packet memory back. The stall count is history, so it survives the close.
	static void CloseSlot(Slot& slot) {
		uint32 stallCount = slot.stallCount;
		slot = Slot{};
		slot.stallCount = stallCount;
	}


	static void AbandonSlot(Slot& slot) {
		slot.armed = false;
		slot.pending = false;
		slot.sendZeroLengthPacket = false;
		slot.source = {};
		slot.target = {};
		slot.position = 0;
	}


	void AbandonTransfer(uint8 address) {
		Slot* slot = FindSlot(address);

		if (slot != nullptr) {
			AbandonSlot(*slot);
		}
	}


	void SetControlHalt(bool halted) {
		slots[SlotIndex(USBEndpoint::ControlOut)].halted = halted;
		slots[SlotIndex(USBEndpoint::ControlIn)].halted = halted;
	}


	// Every endpoint 0 stage starts at DATA1 (USB 2.0 section 8.6.2). The port sets it when the stage is armed.
	static void StartControlStage(Slot& slot, uint8 address) {
		if (USBEndpoint::IsControl(address)) {
			slot.toggle = true;
		}
	}


	// Counts an endpoint 0 stage whose first packet is about to move with DATA0. StartControlStage() sets DATA1, so a nonzero count
	// means the stage was armed some other way. A test asserts it stays zero.
	void CountStageStart(const Slot& slot, uint8 address) {
		if (USBEndpoint::IsControl(address) && slot.position == 0 && !slot.toggle) {
			toggleMismatchCount++;
		}
	}


	// Clears the transfer before the callback, so the callback may arm the next one. A pending SET_ADDRESS takes effect
	// here, on the status stage of that request (the IN completion of endpoint 0), before the core hears about it.
	void FinishTransfer(uint8 address, uint32 byteCount) {
		Slot* slot = FindSlot(address);

		if (slot != nullptr) {
			AbandonSlot(*slot);
		}

		if (address == USBEndpoint::ControlIn && hasPendingAddress) {
			deviceAddress = pendingAddress;
			hasPendingAddress = false;
		}

		if (USBEndpoint::IsControl(address)) {
			controlCompletionCount++;
			lastControlCompletion.address = address;
			lastControlCompletion.byteCount = byteCount;
		}

		// The endpoint's own callback runs with the flag set, so a packet it arms goes out at the next token (see StartInTransfer()).
		if (slot != nullptr) {
			slot->inCompletion = true;
		}

		CallTransferComplete(address, byteCount);
		slot = FindSlot(address);
		if (slot != nullptr) {
			slot->inCompletion = false;
		}
	}


	// Moves one packet into the armed OUT transfer. Returns true when the packet completed the transfer. stored receives the
	// bytes the transfer holds after the packet, which is also the completion count when it returns true.
	bool ReceivePacket(uint8 address, std::span<const uint8> packet, uint32& stored) {
		Slot* slot = FindSlot(address);
		uint32 room = static_cast<uint32>(slot->target.size()) - slot->position;

		// A packet that does not fit is not stored, not even partly. The error comes before the completion.
		if (packet.size() > room) {
			stored = slot->position;
			CallError(Error::DataOverrun);
			FinishTransfer(address, stored);
			return true;
		}

		std::span<uint8> destination = slot->target.subspan(slot->position, packet.size());
		std::copy(packet.begin(), packet.end(), destination.begin());
		slot->position += static_cast<uint32>(packet.size());
		slot->toggle = !slot->toggle;
		stored = slot->position;

		bool shortPacket = packet.size() < static_cast<std::size_t>(slot->config.maxPacketSize);

		// A zero-length packet after a full buffer is not stored here: the host NAKs it until the next transfer is armed.
		if (shortPacket || slot->position == slot->target.size()) {
			FinishTransfer(address, stored);
			return true;
		}

		return false;
	}
};
