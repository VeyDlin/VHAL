#pragma once
#include <Adapter/USBDeviceAdapter.h>
#include <Adapter/Utilities/USBTypes.h>
#include <System/System.h>
#include <concepts>
#include <cstdint>
#include <span>


// Register-level driver of the full-speed USB device peripheral "fsdev" (STM32G4 RM0440, USB chapter; the same block is in
// G0B1/G0C1, RM0444). The code lives here once. A family file derives from USBFsdev<Family> and supplies the Family policy:
//   PmaBase                address of the packet memory (USB_PMAADDR)
//   PmaSize                bytes of packet memory the buffer table and the buffer addresses can reach (1024 on G4)
//   PmaElementStride       bytes between two consecutive 16-bit packet memory elements (2 on G4, halfwords; 4 when the
//                          memory is written as 32-bit words)
//   HardwareEndpointCount  endpoint registers of the peripheral (8)
//   IsClockEnabled()       the peripheral clock is on; the registers cannot be read or written otherwise
//
// The register offsets and bit positions are the same on the families that share the block, so they are used from the
// peripheral base directly. Interrupts: IrqHandler() serves the whole controller; the application calls it from the vector
// that the family file names.
//
// Packet memory access: the buffer table and the buffer addresses are byte offsets from PmaBase. A 16-bit element at byte
// offset n is at PmaBase + (n / 2) * PmaElementStride.


// Offsets from the peripheral base, in bytes. The endpoint registers EPnR are 16-bit at 4 * n.
struct USBFsdevRegister {
	static constexpr uint32 EpBase = 0x00;
	static constexpr uint32 Cntr = 0x40;
	static constexpr uint32 Istr = 0x44;
	static constexpr uint32 Fnr = 0x48;
	static constexpr uint32 Daddr = 0x4C;
	static constexpr uint32 Btable = 0x50;
	static constexpr uint32 Bcdr = 0x58;
};


// EPnR. Writing a bit in the CTR group clears it only when the bit is written as 0 (rc_w0). The STAT and DTOG fields are
// toggled by writing 1, so a write must carry the XOR with the current value (see USBFsdev::EprWrite()). The HAL writes the
// same way: the CTR clear keeps the other CTR bit by writing it as 1 (stm32g4xx_hal_pcd.h, PCD_CLEAR_RX_EP_CTR() line 637),
// and the STAT and DTOG writes toggle only the bits set in the new value (PCD_SET_EP_TX_STATUS() line 481, PCD_SET_EP_RX_STATUS()
// line 506).
struct USBFsdevEpr {
	static constexpr uint16 CtrRx = 0x8000;
	static constexpr uint16 DtogRx = 0x4000;
	static constexpr uint16 RxStatus = 0x3000;
	static constexpr uint16 Setup = 0x0800;
	static constexpr uint16 Type = 0x0600;
	static constexpr uint16 Kind = 0x0100;
	static constexpr uint16 CtrTx = 0x0080;
	static constexpr uint16 DtogTx = 0x0040;
	static constexpr uint16 TxStatus = 0x0030;
	static constexpr uint16 Address = 0x000F;
	// The fields a plain write keeps: address, type and kind.
	static constexpr uint16 Stable = static_cast<uint16>(Address | Type | Kind);
	static constexpr uint16 TypeBulk = 0x0000;
	static constexpr uint16 TypeControl = 0x0200;
	static constexpr uint16 TypeIsochronous = 0x0400;
	static constexpr uint16 TypeInterrupt = 0x0600;
	static constexpr uint16 TxDisabled = 0x0000;
	static constexpr uint16 TxStall = 0x0010;
	static constexpr uint16 TxNak = 0x0020;
	static constexpr uint16 TxValid = 0x0030;
	static constexpr uint16 RxDisabled = 0x0000;
	static constexpr uint16 RxStall = 0x1000;
	static constexpr uint16 RxNak = 0x2000;
	static constexpr uint16 RxValid = 0x3000;
};


struct USBFsdevIstr {
	static constexpr uint16 EpId = 0x000F;
	static constexpr uint16 Dir = 0x0010;
	static constexpr uint16 Esof = 0x0100;
	static constexpr uint16 Sof = 0x0200;
	static constexpr uint16 Reset = 0x0400;
	static constexpr uint16 Susp = 0x0800;
	static constexpr uint16 Wkup = 0x1000;
	static constexpr uint16 Err = 0x2000;
	static constexpr uint16 PmaOvr = 0x4000;
	static constexpr uint16 Ctr = 0x8000;
};


struct USBFsdevCntr {
	static constexpr uint16 FRes = 0x0001;
	static constexpr uint16 LpMode = 0x0004;
	static constexpr uint16 FSusp = 0x0008;
	static constexpr uint16 Resume = 0x0010;
	static constexpr uint16 EsofM = 0x0100;
	static constexpr uint16 SofM = 0x0200;
	static constexpr uint16 ResetM = 0x0400;
	static constexpr uint16 SuspM = 0x0800;
	static constexpr uint16 WkupM = 0x1000;
	static constexpr uint16 ErrM = 0x2000;
	static constexpr uint16 PmaOvrM = 0x4000;
	static constexpr uint16 CtrM = 0x8000;
};


struct USBFsdevDaddr {
	static constexpr uint16 Ef = 0x0080;
	static constexpr uint16 AddressMask = 0x007F;
};


struct USBFsdevBcdr {
	static constexpr uint16 DpPu = 0x8000;
};


struct USBFsdevFnr {
	static constexpr uint16 FrameMask = 0x07FF;
};


// Buffer descriptor of one endpoint, eight bytes at Table + 8 * number. The ADDR fields are packet memory byte offsets.
// COUNT_RX also carries the block size of its buffer: BlockSize32 selects 32-byte blocks, and the count of blocks is
// in bits 14..10 (2-byte blocks when BlockSize32 is clear).
struct USBFsdevBuffer {
	static constexpr uint32 Table = 0;
	static constexpr uint32 EntrySize = 8;
	static constexpr uint32 AddrTx = 0;
	static constexpr uint32 CountTx = 2;
	static constexpr uint32 AddrRx = 4;
	static constexpr uint32 CountRx = 6;
	static constexpr uint16 CountMask = 0x03FF;
	static constexpr uint16 BlockSize32 = 0x8000;
	static constexpr uint16 NumBlockShift = 10;
};


// A family may route the register accesses through its own functions. The host tests do, to model the controller. The STM32
// families do not, and their accesses are 16-bit volatile accesses at the address.
template <typename Family>
concept USBFsdevRegisterHooks = requires(uintptr_t address, uint16 value) {
	{ Family::ReadRegister(address) } -> std::convertible_to<uint16>;
	Family::WriteRegister(address, value);
};


template <typename Family>
class USBFsdev : public USBDeviceAdapter {
public:
	// Software state of one endpoint number. The two directions share the EPnR (type and double-buffer bits), so both are
	// kept here. A double-buffered endpoint serves one direction and uses both buffers of its descriptor: buffer 0 is the
	// ADDR_TX buffer and buffer 1 the ADDR_RX buffer.
	struct EndpointState {
		bool txOpen = false;
		bool rxOpen = false;
		// Two buffers for the direction: a double-buffered bulk endpoint (EP_KIND set), or an isochronous endpoint, which has them
		// without EP_KIND. The STAT copies below serve both.
		bool doubleBuffer = false;
		bool txStalled = false;
		bool rxStalled = false;
		bool txActive = false;
		bool rxActive = false;
		bool txZeroLengthRequested = false;
		bool txZeroLengthQueued = false;
		// Isochronous IN. txDeferred: the packet of StartInTransfer() is not in a buffer yet; the next CTR_TX writes it (see
		// HandleIsoInCompletion()). txCompleting: the completion callback of this endpoint is running, so a call from it arms at once.
		bool txDeferred = false;
		bool txCompleting = false;
		USBEndpointType type = USBEndpointType::Bulk;
		uint16 txPacketSize = 0;
		uint16 rxPacketSize = 0;
		// Packet memory blocks: pmaTx is ADDR_TX, pmaRx is ADDR_RX. A size of 0 means the block is not reserved.
		uint16 pmaTx = 0;
		uint16 pmaRx = 0;
		uint16 sizeTx = 0;
		uint16 sizeRx = 0;
		// STAT of each direction as the adapter last left it, in EPnR bit positions. A double-buffered endpoint is toggled from
		// these copies (see KnownEpr()); every EPnR write updates them, so they match the hardware after each write.
		uint16 txStatus = 0;
		uint16 rxStatus = 0;
		// Double-buffered IN. txArmed is the packet in the buffer the hardware sends next, already released to it. txPrefilled is
		// the packet in the other buffer: copied, not yet released, and released by the completion of txArmed. txConfirmed counts
		// the bytes of the packets the host acknowledged, so ClearStall() can restart from the first one it has not acknowledged.
		std::span<const uint8> txArmed;
		std::span<const uint8> txPrefilled;
		bool txHasPrefilled = false;
		std::span<const uint8> txData;
		uint32 txSize = 0;
		uint32 txQueued = 0;
		uint32 txConfirmed = 0;
		std::span<uint8> rxBuffer;
		uint32 rxDone = 0;
		// Double-buffered OUT. A packet that arrived while no transfer was active stays in its buffer, ADDR_RX when rxHeldBuffer1
		// is set, with rxHeldCount bytes, until StartOutTransfer() copies it out.
		bool rxHeld = false;
		bool rxHeldBuffer1 = false;
		uint32 rxHeldCount = 0;
	};


	// Packet memory below the buffer table is not available to buffers; the table has one entry per hardware endpoint.
	static constexpr uint32 BufferTableSize = static_cast<uint32>(Family::HardwareEndpointCount) * USBFsdevBuffer::EntrySize;
	static constexpr uint32 BufferAlignment = 4;
	// The K state of a remote wakeup is 1 to 15 ms (USB 2.0 section 7.1.7.7); the ESOF interrupt ticks every millisecond while it
	// runs. 10 ms sits in the middle of that window, away from both limits, so the host sees a valid resume whatever the exact
	// timer error.
	static constexpr uint8 RemoteWakeupTicks = 10;


private:
	uintptr_t registerBase = 0;
	bool initialized = false;
	bool connected = false;
	bool suspended = false;
	bool pendingAddressValid = false;
	uint8 pendingAddress = 0;
	uint8 resumeTicksLeft = 0;
	EndpointState endpoints[Family::HardwareEndpointCount];


public:
	explicit USBFsdev(uintptr_t peripheralBase)
		: registerBase(peripheralBase) {
	}


	ResultStatus Connect() override {
		if (!initialized) {
			return ResultStatus::notReady;
		}

		WriteReg(USBFsdevRegister::Bcdr, static_cast<uint16>(ReadReg(USBFsdevRegister::Bcdr) | USBFsdevBcdr::DpPu));
		connected = true;
		return ResultStatus::ok;
	}


	// Abandons transfers in flight without events; endpoints stay open so the core can keep its configuration across a detach.
	// The isochronous IN buffers are emptied, so a packet armed before the detach cannot go out after the next connection.
	// A caller in thread context must mask the USB interrupt (NVIC) around the call: IrqHandler() may change CNTR and the transfer
	// state of the endpoints meanwhile.
	ResultStatus Disconnect() override {
		if (!initialized) {
			return ResultStatus::notReady;
		}

		WriteReg(USBFsdevRegister::Bcdr, static_cast<uint16>(ReadReg(USBFsdevRegister::Bcdr) & ~USBFsdevBcdr::DpPu));
		connected = false;
		for (uint32 number = 0; number < Family::HardwareEndpointCount; ++number) {
			EndpointState& ep = endpoints[number];
			if (ep.txOpen && ep.type == USBEndpointType::Isochronous) {
				ZeroIsoInCounts(static_cast<uint8>(number));
			}

			ep.txActive = false;
			ep.txDeferred = false;
			ep.rxActive = false;
		}

		return ResultStatus::ok;
	}


	bool IsConnected() const override {
		return connected;
	}


	// The address is applied at the status completion of SET_ADDRESS, in FinishIn() of endpoint 0.
	ResultStatus SetAddress(uint8 address) override {
		if (address > USBFsdevDaddr::AddressMask) {
			return ResultStatus::invalidParameter;
		}

		pendingAddress = address;
		pendingAddressValid = true;
		return ResultStatus::ok;
	}


	// The port takes no critical section (USBDeviceAdapter.h). The core calls this from SET_CONFIGURATION, which runs inside
	// IrqHandler(), so the USB vector cannot preempt it there. A caller in thread context must mask the USB interrupt (NVIC)
	// around the call: IrqHandler() may change EPnR of the same number meanwhile, and the write-back here would restore a
	// stale value.
	ResultStatus OpenEndpoint(const EndpointConfig& config) override {
		if (!initialized) {
			return ResultStatus::notReady;
		}

		uint8 number = USBEndpoint::Number(config.address);
		bool isIn = USBEndpoint::IsIn(config.address);
		if (number == 0 || number >= parameters.endpointCount) {
			return ResultStatus::invalidParameter;
		}

		if (config.type == USBEndpointType::Control) {
			return ResultStatus::invalidParameter;
		}

		// doubleBuffer is a bulk option. An isochronous endpoint has its two buffers without it (twoBuffers below), so the flag is refused.
		if (config.doubleBuffer && config.type != USBEndpointType::Bulk) {
			return ResultStatus::invalidParameter;
		}

		if (!IsValidPacketSize(config.type, config.maxPacketSize)) {
			return ResultStatus::invalidParameter;
		}

		EndpointState& ep = endpoints[number];
		// Two buffers for this direction: a double-buffered bulk endpoint, and every isochronous endpoint. The isochronous endpoint works
		// on the buffer the hardware selects with DTOG and keeps the other one for the next frame (see ArmIsoIn()).
		bool twoBuffers = config.doubleBuffer || config.type == USBEndpointType::Isochronous;
		bool sameOpen = isIn ? ep.txOpen : ep.rxOpen;
		bool otherOpen = isIn ? ep.rxOpen : ep.txOpen;
		if (sameOpen && (isIn ? ep.txActive : ep.rxActive)) {
			return ResultStatus::busy;
		}

		// The EPnR type bits are shared by both directions of the number, so the other direction must have the same type.
		if (otherOpen && ep.type != config.type) {
			return ResultStatus::invalidParameter;
		}

		// The register cannot hold double buffering beside the other direction. That is a capability the port does not have
		// here, so it is notSupported and the endpoints stay as they were. A single-buffered request beside a double-buffered
		// other direction is a conflict the caller created, so it is invalidParameter.
		if (otherOpen && twoBuffers) {
			return ResultStatus::notSupported;
		}

		if (otherOpen && ep.doubleBuffer) {
			return ResultStatus::invalidParameter;
		}

		uint16 size = PmaBlockSize(config.maxPacketSize);
		// A double-buffered endpoint holds both blocks, so reopening it in either direction releases both.
		bool replacesTx = isIn || twoBuffers || ep.doubleBuffer;
		bool replacesRx = !isIn || twoBuffers || ep.doubleBuffer;
		uint16 savedSizeTx = ep.sizeTx;
		uint16 savedSizeRx = ep.sizeRx;
		if (replacesTx) {
			ep.sizeTx = 0;
		}

		if (replacesRx) {
			ep.sizeRx = 0;
		}

		uint16 firstBlock = 0;
		uint16 secondBlock = 0;
		bool fits = FindPmaBlock(size, 0, 0, firstBlock);
		if (fits && twoBuffers) {
			fits = FindPmaBlock(size, firstBlock, size, secondBlock);
		}

		if (!fits) {
			ep.sizeTx = savedSizeTx;
			ep.sizeRx = savedSizeRx;
			return ResultStatus::outOfMemory;
		}

		if (twoBuffers) {
			ep.pmaTx = firstBlock;
			ep.sizeTx = size;
			ep.pmaRx = secondBlock;
			ep.sizeRx = size;
		} else if (isIn) {
			ep.pmaTx = firstBlock;
			ep.sizeTx = size;
		} else {
			ep.pmaRx = firstBlock;
			ep.sizeRx = size;
		}

		ep.type = config.type;
		// A reopened direction starts with empty buffers: a prefilled IN packet or a held OUT packet of the old endpoint is dropped.
		if (replacesTx) {
			ep.txHasPrefilled = false;
		}

		if (replacesRx) {
			ep.rxHeld = false;
		}

		// A single-buffered endpoint's STAT is the register itself. Its copies go stale when the hardware ends a transaction that the
		// port has not served yet, so they are refreshed here, before the endpoint turns double-buffered. The toggles below then
		// start from the real STAT.
		if (!ep.doubleBuffer) {
			uint16 epr = ReadEpr(number);
			ep.txStatus = static_cast<uint16>(epr & USBFsdevEpr::TxStatus);
			ep.rxStatus = static_cast<uint16>(epr & USBFsdevEpr::RxStatus);
		}

		if (isIn) {
			ep.txOpen = true;
			ep.txPacketSize = config.maxPacketSize;
			ep.txStalled = false;
		} else {
			ep.rxOpen = true;
			ep.rxPacketSize = config.maxPacketSize;
			ep.rxStalled = false;
		}

		uint16 receiveCount = EncodeReceiveCount(size);
		if (isIn) {
			WriteBd(number, USBFsdevBuffer::AddrTx, ep.pmaTx);
			WriteBd(number, USBFsdevBuffer::CountTx, 0);
		} else if (twoBuffers) {
			WriteBd(number, USBFsdevBuffer::AddrTx, ep.pmaTx);
			WriteBd(number, USBFsdevBuffer::CountTx, receiveCount);
		}

		if (twoBuffers) {
			WriteBd(number, USBFsdevBuffer::AddrRx, ep.pmaRx);
			WriteBd(number, USBFsdevBuffer::CountRx, isIn ? static_cast<uint16>(0) : receiveCount);
		} else if (!isIn) {
			WriteBd(number, USBFsdevBuffer::AddrRx, ep.pmaRx);
			WriteBd(number, USBFsdevBuffer::CountRx, receiveCount);
		}

		uint16 typeBits = TypeBitsOf(config.type);
		// EP_KIND is the bulk double buffer. An isochronous endpoint has its two buffers without it, so the bit stays clear for it.
		uint16 kindBits = config.doubleBuffer ? USBFsdevEpr::Kind : static_cast<uint16>(0);
		uint16 dtogMask = 0;
		if (twoBuffers) {
			dtogMask = static_cast<uint16>(USBFsdevEpr::DtogTx | USBFsdevEpr::DtogRx);
		} else if (isIn) {
			dtogMask = USBFsdevEpr::DtogTx;
		} else {
			dtogMask = USBFsdevEpr::DtogRx;
		}

		uint16 statMask = isIn ? USBFsdevEpr::TxStatus : USBFsdevEpr::RxStatus;
		// The status an open endpoint starts in. Bulk and interrupt start with NAK, which holds the host until the first transfer is armed. An
		// isochronous endpoint has no NAK: its status stays VALID from open, and it answers every token (see ArmIsoIn()).
		uint16 statIdle = 0;
		if (config.type == USBEndpointType::Isochronous) {
			statIdle = isIn ? USBFsdevEpr::TxValid : USBFsdevEpr::RxValid;
		} else {
			statIdle = isIn ? USBFsdevEpr::TxNak : USBFsdevEpr::RxNak;
		}

		uint16 stableBits = static_cast<uint16>(number | typeBits | kindBits);
		// The flag is set after the write. The write then takes its STAT from the copies while the endpoint still counts as double-buffered,
		// so a reopen from double to single does not take the masked STAT read of a buffer conflict.
		// The direction's CTR flag is cleared by this write. A flag the controller holds from a token before the close is a completion of the old
		// endpoint, and the interrupt would serve it against the transfer armed now. The other direction keeps its flag, because that one may be
		// a completion the interrupt has not served yet.
		uint16 ctrBit = isIn ? USBFsdevEpr::CtrTx : USBFsdevEpr::CtrRx;
		EprWriteStable(number, stableBits, ctrBit, static_cast<uint16>(dtogMask | statMask), statIdle);
		ep.doubleBuffer = twoBuffers;
		return ResultStatus::ok;
	}


	ResultStatus CloseEndpoint(uint8 address) override {
		uint8 number = USBEndpoint::Number(address);
		if (number == 0) {
			return ResultStatus::invalidParameter;
		}

		if (number >= Family::HardwareEndpointCount) {
			return ResultStatus::notExist;
		}

		EndpointState& ep = endpoints[number];
		bool isIn = USBEndpoint::IsIn(address);
		if (isIn ? !ep.txOpen : !ep.rxOpen) {
			return ResultStatus::notExist;
		}

		if (isIn) {
			ep.txOpen = false;
			ep.txActive = false;
			ep.txDeferred = false;
			ep.txStalled = false;
			ep.txHasPrefilled = false;
			ep.sizeTx = 0;
			// STAT goes to DISABLED first, so the controller answers no token for the endpoint from this write on. Only then do the buffers of a
			// closed isochronous IN go empty, as in Disconnect(): nothing armed before the close goes out later.
			EprWrite(number, 0, USBFsdevEpr::TxStatus, USBFsdevEpr::TxDisabled);
			if (ep.type == USBEndpointType::Isochronous) {
				ZeroIsoInCounts(number);
			}
		} else {
			ep.rxOpen = false;
			ep.rxActive = false;
			ep.rxStalled = false;
			ep.rxHeld = false;
			ep.sizeRx = 0;
			EprWrite(number, 0, USBFsdevEpr::RxStatus, USBFsdevEpr::RxDisabled);
		}

		if (ep.doubleBuffer) {
			ep.sizeTx = 0;
			ep.sizeRx = 0;
		}

		if (!ep.txOpen && !ep.rxOpen) {
			// The write runs while the endpoint still counts as double-buffered, so the status comes from the copies. The kind bit
			// is cleared by this write (the number is the stable value) and the flag follows it.
			EprWriteStable(number, number, 0, static_cast<uint16>(USBFsdevEpr::DtogTx | USBFsdevEpr::DtogRx), 0);
			ep.doubleBuffer = false;
		}

		return ResultStatus::ok;
	}


	void CloseAllEndpoints() override {
		for (uint8 number = 1; number < Family::HardwareEndpointCount; ++number) {
			CloseEndpoint(USBEndpoint::MakeIn(number));
			CloseEndpoint(USBEndpoint::MakeOut(number));
		}
	}


	// Starts a transfer on an IN address. A transfer may be armed while the endpoint is stalled: it waits for ClearStall().
	// A caller in thread context must mask the USB interrupt around the call. System::CriticalSection() does, because it masks
	// every interrupt on this port, and so does masking the USB line in NVIC. IrqHandler() may complete the endpoint or change
	// its EPnR meanwhile.
	ResultStatus StartInTransfer(uint8 address, std::span<const uint8> data, bool sendZeroLengthPacket) override {
		if (!USBEndpoint::IsIn(address)) {
			return ResultStatus::invalidParameter;
		}

		if (!connected) {
			return ResultStatus::notReady;
		}

		uint8 number = USBEndpoint::Number(address);
		if (number >= Family::HardwareEndpointCount || !endpoints[number].txOpen) {
			return ResultStatus::notExist;
		}

		EndpointState& ep = endpoints[number];
		if (ep.txActive) {
			return ResultStatus::busy;
		}

		// An isochronous transfer is one packet of one frame, so the data fits one packet. An empty span is a zero-length packet.
		if (ep.type == USBEndpointType::Isochronous && data.size() > ep.txPacketSize) {
			return ResultStatus::invalidParameter;
		}

		ep.txActive = true;
		ep.txData = data;
		ep.txSize = static_cast<uint32>(data.size());
		ep.txQueued = 0;
		ep.txConfirmed = 0;
		ep.txHasPrefilled = false;
		ep.txZeroLengthRequested = sendZeroLengthPacket;
		ep.txZeroLengthQueued = false;
		if (ep.type == USBEndpointType::Isochronous) {
			// Armed from the completion callback of this endpoint, the packet is written now: that token has finished, so the buffer
			// DTOG_TX selects is free, and the packet goes out at the next token. Armed from anywhere else, the write waits for the next
			// CTR_TX (HandleIsoInCompletion()). The token that comes first sends a zero-length packet, so the packet goes out at the one after.
			if (ep.txCompleting) {
				ArmIsoIn(number, data);
			} else {
				ep.txDeferred = true;
			}
		} else if (ep.doubleBuffer) {
			// The start keeps DTOG_TX as it is: the buffer the hardware sends next carries the DATA PID the host expects.
			LoadDoubleBufferIn(number, (ReadEpr(number) & USBFsdevEpr::DtogTx) != 0);
		} else {
			ArmNextIn(number);
		}

		return ResultStatus::ok;
	}


	// Arms an OUT transfer on an OUT address, or on endpoint 0 for a data or status stage.
	// A caller in thread context must mask the USB interrupt around the call. System::CriticalSection() does, because it masks
	// every interrupt on this port, and so does masking the USB line in NVIC. IrqHandler() may complete the endpoint or change
	// its EPnR meanwhile.
	ResultStatus StartOutTransfer(uint8 address, std::span<uint8> buffer) override {
		if (USBEndpoint::IsIn(address)) {
			return ResultStatus::invalidParameter;
		}

		if (!connected) {
			return ResultStatus::notReady;
		}

		uint8 number = USBEndpoint::Number(address);
		if (number >= Family::HardwareEndpointCount || !endpoints[number].rxOpen) {
			return ResultStatus::notExist;
		}

		EndpointState& ep = endpoints[number];
		if (ep.rxActive) {
			return ResultStatus::busy;
		}

		// A completed packet the interrupt has not served yet is taken first. With rxActive still false it is held, so the hand-back below
		// cannot give its buffer to the hardware, and the held-packet path delivers it.
		if (ep.doubleBuffer && (ReadEpr(number) & USBFsdevEpr::CtrRx) != 0) {
			HandleOutCompletion(number);
		}

		ep.rxActive = true;
		ep.rxBuffer = buffer;
		ep.rxDone = 0;
		// An isochronous OUT has nothing to write when it is armed. The hardware stores the next packet whatever the software has set, and the
		// completion hands the packet on (HandleIsoOutCompletion()).
		if (ep.type == USBEndpointType::Isochronous) {
			return ResultStatus::ok;
		}

		if (ep.doubleBuffer && ep.rxHeld) {
			// A packet that arrived before the transfer is delivered first. The buffers are handed on only when the transfer goes on.
			if (!DeliverHeldOut(number)) {
				ArmOut(number);
			}

			return ResultStatus::ok;
		}

		ArmOut(number);
		return ResultStatus::ok;
	}


	// Halts both directions of endpoint 0 with one call, and one direction of the other endpoints.
	// A caller in thread context must mask the USB interrupt (NVIC) around the call: IrqHandler() may change EPnR of the same number meanwhile.
	ResultStatus Stall(uint8 address) override {
		uint8 number = USBEndpoint::Number(address);
		if (number >= Family::HardwareEndpointCount) {
			return ResultStatus::notExist;
		}

		EndpointState& ep = endpoints[number];

		// The direction must be open first, so a closed one is notExist even when the other direction is isochronous. An isochronous endpoint
		// has no halt (USB 2.0 section 5.6.4): the request is refused and nothing changes. The type is the one of the open direction.
		if (USBEndpoint::IsIn(address) ? !ep.txOpen : !ep.rxOpen) {
			return ResultStatus::notExist;
		}

		if (ep.type == USBEndpointType::Isochronous) {
			return ResultStatus::invalidParameter;
		}

		if (number == 0) {
			if (!ep.txOpen) {
				return ResultStatus::notExist;
			}

			ep.txStalled = true;
			ep.rxStalled = true;
			ep.txActive = false;
			ep.rxActive = false;
			uint16 statusBits = static_cast<uint16>(USBFsdevEpr::TxStatus | USBFsdevEpr::RxStatus);
			uint16 stallBits = static_cast<uint16>(USBFsdevEpr::TxStall | USBFsdevEpr::RxStall);
			EprWrite(0, 0, statusBits, stallBits);
			return ResultStatus::ok;
		}

		if (USBEndpoint::IsIn(address)) {
			if (!ep.txOpen) {
				return ResultStatus::notExist;
			}

			ep.txStalled = true;
			ep.txActive = false;
			EprWrite(number, 0, USBFsdevEpr::TxStatus, USBFsdevEpr::TxStall);
		} else {
			if (!ep.rxOpen) {
				return ResultStatus::notExist;
			}

			ep.rxStalled = true;
			ep.rxActive = false;
			EprWrite(number, 0, USBFsdevEpr::RxStatus, USBFsdevEpr::RxStall);
		}

		return ResultStatus::ok;
	}


	// Clears the halt and resets the data toggle to DATA0 (USB 2.0 section 9.4.5), also when the endpoint is not halted. A
	// transfer armed while stalled starts now. On a double-buffered endpoint the toggle is the buffer select as well, so the
	// active transfer is restarted in buffer 0 (see ClearDoubleBufferIn() and ClearDoubleBufferOut()). An isochronous endpoint is never
	// halted and has no toggle to reset, so the call is acknowledged and changes nothing. A caller in thread context must mask
	// the USB interrupt (NVIC) around the call: IrqHandler() may change EPnR of the same number meanwhile.
	ResultStatus ClearStall(uint8 address) override {
		uint8 number = USBEndpoint::Number(address);
		if (number >= Family::HardwareEndpointCount) {
			return ResultStatus::notExist;
		}

		if (IsIsochronous(number)) {
			bool open = USBEndpoint::IsIn(address) ? endpoints[number].txOpen : endpoints[number].rxOpen;
			return open ? ResultStatus::ok : ResultStatus::notExist;
		}

		EndpointState& ep = endpoints[number];
		if (number == 0) {
			if (!ep.txOpen) {
				return ResultStatus::notExist;
			}

			ep.txStalled = false;
			ep.rxStalled = false;
			uint16 txStatus = ep.txActive ? USBFsdevEpr::TxValid : USBFsdevEpr::TxNak;
			uint16 rxStatus = ep.rxActive ? USBFsdevEpr::RxValid : USBFsdevEpr::RxNak;
			uint16 statusBits = static_cast<uint16>(USBFsdevEpr::TxStatus | USBFsdevEpr::RxStatus);
			uint16 dtogBits = static_cast<uint16>(USBFsdevEpr::DtogTx | USBFsdevEpr::DtogRx);
			EprWrite(0, 0, static_cast<uint16>(statusBits | dtogBits), static_cast<uint16>(txStatus | rxStatus));
			return ResultStatus::ok;
		}

		if (USBEndpoint::IsIn(address)) {
			if (!ep.txOpen) {
				return ResultStatus::notExist;
			}

			ep.txStalled = false;
			if (ep.doubleBuffer) {
				ClearDoubleBufferIn(number);
			} else {
				uint16 txStatus = ep.txActive ? USBFsdevEpr::TxValid : USBFsdevEpr::TxNak;
				EprWrite(number, 0, static_cast<uint16>(USBFsdevEpr::DtogTx | USBFsdevEpr::TxStatus), txStatus);
			}
		} else {
			if (!ep.rxOpen) {
				return ResultStatus::notExist;
			}

			ep.rxStalled = false;
			if (ep.doubleBuffer) {
				ClearDoubleBufferOut(number);
			} else {
				uint16 rxStatus = ep.rxActive ? USBFsdevEpr::RxValid : USBFsdevEpr::RxNak;
				EprWrite(number, 0, static_cast<uint16>(USBFsdevEpr::DtogRx | USBFsdevEpr::RxStatus), rxStatus);
			}
		}

		return ResultStatus::ok;
	}


	bool IsStalled(uint8 address) const override {
		uint8 number = USBEndpoint::Number(address);
		if (number >= Family::HardwareEndpointCount) {
			return false;
		}

		const EndpointState& ep = endpoints[number];
		if (number == 0) {
			return ep.txStalled;
		}

		return USBEndpoint::IsIn(address) ? ep.txStalled : ep.rxStalled;
	}


	// Starts resume signalling. The ESOF interrupt counts the milliseconds, then the port ends the K state and raises
	// onResume. The counter needs ESOF ticks while the bus is suspended; that is the controller's behaviour in RM0440.
	// A caller in thread context must mask the USB interrupt (NVIC) around the call: IrqHandler() may change CNTR meanwhile, and the
	// write-back here would restore a stale value.
	ResultStatus RemoteWakeup() override {
		if (!initialized || !connected || !suspended || resumeTicksLeft != 0) {
			return ResultStatus::notReady;
		}

		SetCntrBits(
			static_cast<uint16>(USBFsdevCntr::Resume | USBFsdevCntr::EsofM),
			static_cast<uint16>(USBFsdevCntr::FSusp | USBFsdevCntr::LpMode)
		);
		resumeTicksLeft = RemoteWakeupTicks;
		return ResultStatus::ok;
	}


	uint16 GetFrameNumber() const override {
		return static_cast<uint16>(ReadReg(USBFsdevRegister::Fnr) & USBFsdevFnr::FrameMask);
	}


	// Serves every pending flag of the controller. Called from the application's vector (USB_LP_IRQHandler on G4).
	void IrqHandler() override {
		uint16 istr = ReadIstr();
		// The fast path runs before the flags, so the buffer release is the first write of the interrupt. A completion that comes
		// with a reset, suspend, wakeup, error or PMA overrun keeps the order of the flags below, and the loop serves it.
		if (IsFastInCompletion(istr)) {
			ServeFastInCompletion(static_cast<uint8>(istr & USBFsdevIstr::EpId));
		}

		if ((istr & USBFsdevIstr::Reset) != 0) {
			ClearIstr(USBFsdevIstr::Reset);
			HandleBusReset();
		}

		if ((istr & USBFsdevIstr::Susp) != 0) {
			HandleSuspend();
		}

		if ((istr & USBFsdevIstr::Wkup) != 0) {
			ClearIstr(USBFsdevIstr::Wkup);
			HandleWakeup();
		}

		if ((istr & USBFsdevIstr::Err) != 0) {
			ClearIstr(USBFsdevIstr::Err);
			CallError(Error::Unknown);
		}

		if ((istr & USBFsdevIstr::PmaOvr) != 0) {
			ClearIstr(USBFsdevIstr::PmaOvr);
			CallError(Error::Unknown);
		}

		if ((istr & USBFsdevIstr::Sof) != 0) {
			ClearIstr(USBFsdevIstr::Sof);
			if (parameters.startOfFrameEvents) {
				CallStartOfFrame(GetFrameNumber());
			}
		}

		if ((istr & USBFsdevIstr::Esof) != 0) {
			ClearIstr(USBFsdevIstr::Esof);
			HandleExpectedSof();
		}

		// The transfer interrupts are serviced one endpoint at a time; ISTR shows the next one until none is left.
		while ((ReadIstr() & USBFsdevIstr::Ctr) != 0) {
			if (!HandleCorrectTransfer()) {
				break;
			}
		}
	}


protected:
	// Power-up: PDWN is cleared with FRES set, and FRES is held for 10 us. The reference manual's tSTARTUP is 1 us (USB chapter,
	// RM0440), so the 10 us hold is margin on top of that, not a value the hardware needs. Then the endpoints and packet memory
	// are reset, endpoint 0 gets its two 64-byte buffers, and the interrupts are enabled. The pull-up stays off.
	ResultStatus Initialization() override {
		// Set first, so a failed initialization always leaves the adapter uninitialized, even after an earlier success.
		initialized = false;
		connected = false;
		if (!Family::IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		if (parameters.endpointCount == 0 || parameters.endpointCount > Family::HardwareEndpointCount) {
			return ResultStatus::invalidParameter;
		}

		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		suspended = false;
		pendingAddressValid = false;
		resumeTicksLeft = 0;
		WriteReg(USBFsdevRegister::Cntr, USBFsdevCntr::FRes);
		WaitMicroseconds(10);
		WriteReg(USBFsdevRegister::Cntr, 0);
		WriteReg(USBFsdevRegister::Istr, 0);
		WriteReg(USBFsdevRegister::Btable, static_cast<uint16>(USBFsdevBuffer::Table));
		for (uint32 number = 0; number < Family::HardwareEndpointCount; ++number) {
			// The first write clears the kind bit, so the status reads below are not masked as buffer-conflict reads. Then STAT
			// and DTOG go to DISABLED and DATA0 whatever a previous session left, and the status copies follow the write.
			WriteReg(USBFsdevRegister::EpBase + 4U * number, 0);
			endpoints[number] = EndpointState();
			EprWriteStable(
				static_cast<uint8>(number),
				0,
				0,
				static_cast<uint16>(USBFsdevEpr::DtogTx | USBFsdevEpr::DtogRx | USBFsdevEpr::TxStatus | USBFsdevEpr::RxStatus),
				0
			);
		}

		uint16 controlTx = 0;
		uint16 controlRx = 0;
		if (!FindPmaBlock(ControlMaxPacketSize, 0, 0, controlTx)) {
			return ResultStatus::outOfMemory;
		}

		if (!FindPmaBlock(ControlMaxPacketSize, controlTx, ControlMaxPacketSize, controlRx)) {
			return ResultStatus::outOfMemory;
		}

		endpoints[0].pmaTx = controlTx;
		endpoints[0].sizeTx = ControlMaxPacketSize;
		endpoints[0].pmaRx = controlRx;
		endpoints[0].sizeRx = ControlMaxPacketSize;
		endpoints[0].txPacketSize = ControlMaxPacketSize;
		endpoints[0].rxPacketSize = ControlMaxPacketSize;
		uint16 mask = static_cast<uint16>(USBFsdevCntr::CtrM | USBFsdevCntr::ResetM | USBFsdevCntr::SuspM | USBFsdevCntr::WkupM | USBFsdevCntr::ErrM | USBFsdevCntr::PmaOvrM);
		if (parameters.startOfFrameEvents) {
			mask = static_cast<uint16>(mask | USBFsdevCntr::SofM);
		}

		WriteReg(USBFsdevRegister::Cntr, mask);
		WriteReg(USBFsdevRegister::Daddr, USBFsdevDaddr::Ef);
		WriteReg(USBFsdevRegister::Bcdr, static_cast<uint16>(ReadReg(USBFsdevRegister::Bcdr) & ~USBFsdevBcdr::DpPu));
		initialized = true;
		return AfterInitialization();
	}


	volatile uint16* Reg(uint32 offset) const {
		return reinterpret_cast<volatile uint16*>(registerBase + offset);
	}


	uint16 ReadReg(uint32 offset) const {
		if constexpr (USBFsdevRegisterHooks<Family>) {
			return Family::ReadRegister(registerBase + offset);
		} else {
			return *Reg(offset);
		}
	}


	void WriteReg(uint32 offset, uint16 value) const {
		if constexpr (USBFsdevRegisterHooks<Family>) {
			Family::WriteRegister(registerBase + offset, value);
		} else {
			*Reg(offset) = value;
		}
	}


	void SetCntrBits(uint16 set, uint16 clear) const {
		uint16 value = ReadReg(USBFsdevRegister::Cntr);
		value = static_cast<uint16>((value | set) & ~clear);
		WriteReg(USBFsdevRegister::Cntr, value);
	}


	uint16 ReadIstr() const {
		return ReadReg(USBFsdevRegister::Istr);
	}


	// ISTR flags are cleared by writing 0. Writing 1 leaves a flag as it is, and the read-only bits ignore the write. The HAL clears
	// each flag with __HAL_PCD_CLEAR_FLAG() (stm32g4xx_hal_pcd.h line 201), for example in HAL_PCD_IRQHandler() at
	// stm32g4xx_hal_pcd.c line 944.
	void ClearIstr(uint16 flags) const {
		WriteReg(USBFsdevRegister::Istr, static_cast<uint16>(~flags));
	}


	uint16 ReadEpr(uint8 number) const {
		return ReadReg(USBFsdevRegister::EpBase + 4U * number);
	}


	// The EPnR value the adapter works from. The DTOG and CTR bits are read as they are. The STAT bits of a double-buffered
	// endpoint come from the software copies: RM0440 masks the STAT read as NAK during a buffer conflict, and a conflict is
	// normal here (DTOG_TX equal to SW_BUF after every packet), so a read-back STAT is not the status the controller holds.
	// A single-buffered endpoint has no such masking, and its STAT is read.
	uint16 KnownEpr(uint8 number) const {
		return KnownEpr(number, ReadEpr(number));
	}


	// KnownEpr() of a value the caller has read already, so the register is not read again.
	uint16 KnownEpr(uint8 number, uint16 epr) const {
		const EndpointState& ep = endpoints[number];
		if (!ep.doubleBuffer) {
			return epr;
		}

		uint16 statusBits = static_cast<uint16>(USBFsdevEpr::TxStatus | USBFsdevEpr::RxStatus);
		return static_cast<uint16>((epr & ~statusBits) | ep.txStatus | ep.rxStatus);
	}


	// Writes EPnR from known, the value KnownEpr() gives, so nothing is read here. The address, type and kind are set to stableBits.
	// CTR flags that are not in clearCtr are written as 1, which leaves them as they are. The STAT and DTOG bits in toggleMask are set
	// to the bits of value: the write carries the XOR with the known state, because writing 1 toggles. The status copies follow the write.
	void WriteEprKnown(uint8 number, uint16 known, uint16 stableBits, uint16 clearCtr, uint16 toggleMask, uint16 value) {
		uint16 flips = static_cast<uint16>((known & toggleMask) ^ (value & toggleMask));
		uint16 ctr = static_cast<uint16>((USBFsdevEpr::CtrRx | USBFsdevEpr::CtrTx) & ~clearCtr);
		WriteReg(USBFsdevRegister::EpBase + 4U * number, static_cast<uint16>(stableBits | ctr | flips));
		uint16 next = static_cast<uint16>(known ^ flips);
		EndpointState& ep = endpoints[number];
		ep.txStatus = static_cast<uint16>(next & USBFsdevEpr::TxStatus);
		ep.rxStatus = static_cast<uint16>(next & USBFsdevEpr::RxStatus);
	}


	// Writes EPnR with the address, type and kind set to stableBits (see WriteEprKnown()).
	void EprWriteStable(uint8 number, uint16 stableBits, uint16 clearCtr, uint16 toggleMask, uint16 value) {
		WriteEprKnown(number, KnownEpr(number), stableBits, clearCtr, toggleMask, value);
	}


	// Writes EPnR with the address, type and kind kept as they are. One register read, for the known value.
	void EprWrite(uint8 number, uint16 clearCtr, uint16 toggleMask, uint16 value) {
		uint16 known = KnownEpr(number);
		WriteEprKnown(number, known, static_cast<uint16>(known & USBFsdevEpr::Stable), clearCtr, toggleMask, value);
	}


	volatile uint16* PmaElement(uint32 offset) const {
		return reinterpret_cast<volatile uint16*>(Family::PmaBase + (offset / 2) * Family::PmaElementStride);
	}


	uint16 ReadPma(uint32 offset) const {
		return *PmaElement(offset);
	}


	void WritePma(uint32 offset, uint16 value) const {
		*PmaElement(offset) = value;
	}


	// Packet memory is written as 16-bit elements, the first byte in the low half, as the controller expects.
	void CopyToPma(uint32 offset, std::span<const uint8> data) const {
		for (uint32 index = 0; index < data.size(); index += 2) {
			uint16 low = data[index];
			uint16 high = index + 1 < data.size() ? data[index + 1] : static_cast<uint16>(0);
			WritePma(offset + index, static_cast<uint16>(low | (high << 8)));
		}
	}


	void CopyFromPma(uint32 offset, std::span<uint8> target) const {
		for (uint32 index = 0; index < target.size(); index += 2) {
			uint16 value = ReadPma(offset + index);
			target[index] = static_cast<uint8>(value & 0xFF);
			if (index + 1 < target.size()) {
				target[index + 1] = static_cast<uint8>(value >> 8);
			}
		}
	}


	void WriteBd(uint8 number, uint32 field, uint16 value) const {
		WritePma(USBFsdevBuffer::Table + number * USBFsdevBuffer::EntrySize + field, value);
	}


	// Returns the first aligned block of size bytes that is free in the packet memory above the buffer table. The
	// reserved block is taken as used, so the second block of a double-buffered endpoint cannot overlap the first.
	bool FindPmaBlock(uint16 size, uint16 reservedAddress, uint16 reservedSize, uint16& address) const {
		uint32 candidate = BufferTableSize;
		while (candidate + size <= Family::PmaSize) {
			uint32 conflictEnd = 0;
			if (reservedSize != 0) {
				conflictEnd = ConflictEnd(candidate, size, reservedAddress, reservedSize);
			}

			for (uint32 number = 0; number < Family::HardwareEndpointCount && conflictEnd == 0; ++number) {
				const EndpointState& other = endpoints[number];
				if (other.sizeTx != 0) {
					conflictEnd = ConflictEnd(candidate, size, other.pmaTx, other.sizeTx);
				}

				if (conflictEnd == 0 && other.sizeRx != 0) {
					conflictEnd = ConflictEnd(candidate, size, other.pmaRx, other.sizeRx);
				}
			}

			if (conflictEnd == 0) {
				address = static_cast<uint16>(candidate);
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


	static uint32 AlignUp(uint32 value) {
		return (value + BufferAlignment - 1) & ~(BufferAlignment - 1);
	}


	// COUNT_RX for a buffer of size bytes (even). NUM_BLOCK is a 5-bit field in COUNT_RX (RM0440, USB chapter), so 2-byte blocks
	// reach 31 * 2 = 62 bytes, the limit used here. Above that BL_SIZE selects 32-byte blocks and NUM_BLOCK = size / 32 - 1
	// (1 for 64 bytes).
	static uint16 EncodeReceiveCount(uint16 size) {
		if (size <= 62) {
			return static_cast<uint16>((size / 2) << USBFsdevBuffer::NumBlockShift);
		}

		return static_cast<uint16>(USBFsdevBuffer::BlockSize32 | (((size / 32) - 1) << USBFsdevBuffer::NumBlockShift));
	}


	static bool IsValidPacketSize(USBEndpointType type, uint16 size) {
		if (type == USBEndpointType::Bulk) {
			return size == 8 || size == 16 || size == 32 || size == 64;
		}

		if (type == USBEndpointType::Isochronous) {
			return size >= 1 && size <= 1023;
		}

		return size >= 1 && size <= 64;
	}


	// The EPnR type bits of an endpoint type (EP_TYPE, RM0440 USB chapter). Control is never opened here.
	static uint16 TypeBitsOf(USBEndpointType type) {
		if (type == USBEndpointType::Interrupt) {
			return USBFsdevEpr::TypeInterrupt;
		}

		if (type == USBEndpointType::Isochronous) {
			return USBFsdevEpr::TypeIsochronous;
		}

		return USBFsdevEpr::TypeBulk;
	}


	// The packet memory block of a packet size, in bytes. The block is a whole number of 16-bit elements, so an odd size takes one more
	// byte. A block above 62 bytes is counted in 32-byte blocks (COUNT_RX BL_SIZE, see EncodeReceiveCount()), so it is rounded up to 32:
	// that count is the only one the field can state. Sizes up to 64 are unchanged, and so are bulk and interrupt endpoints.
	static uint16 PmaBlockSize(uint16 maxPacketSize) {
		if (maxPacketSize > 62) {
			return static_cast<uint16>((maxPacketSize + 31U) & ~31U);
		}

		return static_cast<uint16>((maxPacketSize + 1U) & ~1U);
	}


	// Busy-wait, not a timer: the port keeps no timer, so none has to be configured for it. The count is approximate and scales with
	// SystemCoreClock, which is enough for the 10 us FRES hold.
	static void WaitMicroseconds(uint32 microseconds) {
		uint32 loops = microseconds * (SystemCoreClock / 1000000U);
		for (uint32 index = 0; index < loops; ++index) {
			__asm volatile("nop");
		}
	}


	// Single-buffered IN: the packet goes into the TX buffer and the endpoint is armed. The double-buffered IN has its own path
	// (LoadDoubleBufferIn() and HandleDoubleBufferInCompletion()).
	void ArmIn(uint8 number, std::span<const uint8> data) {
		EndpointState& ep = endpoints[number];
		uint16 status = ep.txStalled ? USBFsdevEpr::TxStall : USBFsdevEpr::TxValid;
		CopyToPma(ep.pmaTx, data);
		WriteBd(number, USBFsdevBuffer::CountTx, static_cast<uint16>(data.size()));
		EprWrite(number, 0, USBFsdevEpr::TxStatus, status);
	}


	// Copies a packet into one buffer of a double-buffered IN endpoint and sets its count. buffer1 selects ADDR_RX, else ADDR_TX. The
	// hardware does not send the buffer until it is released (LoadDoubleBufferIn(), HandleDoubleBufferInCompletion()).
	void FillInBuffer(uint8 number, bool buffer1, std::span<const uint8> data) {
		EndpointState& ep = endpoints[number];
		CopyToPma(buffer1 ? ep.pmaRx : ep.pmaTx, data);
		WriteBd(number, buffer1 ? USBFsdevBuffer::CountRx : USBFsdevBuffer::CountTx, static_cast<uint16>(data.size()));
	}


	// Double-buffered IN, the hardware sends buffer DTOG_TX and toggles DTOG_TX when a packet is done. A buffer is ready for the hardware
	// when DTOG_RX differs from DTOG_TX, so releasing a buffer hands it over by making them differ. The next completion flips DTOG_TX onto
	// the other buffer, so one SW_BUF write per completion is enough to keep the hardware fed (HAL_PCD_EP_DB_Transmit() in the G4 HAL).
	// The start, and a restart from ClearStall(), fill both buffers and release the first one.
	void LoadDoubleBufferIn(uint8 number, bool buffer1) {
		EndpointState& ep = endpoints[number];
		std::span<const uint8> first;
		NextInChunk(number, first);
		FillInBuffer(number, buffer1, first);
		ep.txArmed = first;
		std::span<const uint8> second;
		ep.txHasPrefilled = NextInChunk(number, second);
		if (ep.txHasPrefilled) {
			FillInBuffer(number, !buffer1, second);
			ep.txPrefilled = second;
		}

		// One write selects buffer1 with DTOG_TX (DATA0 for buffer 0), releases it with SW_BUF and sets the STAT. The buffers are
		// filled before it, so the hardware never sends a buffer that is not ready.
		uint16 select = buffer1 ? USBFsdevEpr::DtogTx : static_cast<uint16>(0);
		uint16 release = buffer1 ? static_cast<uint16>(0) : USBFsdevEpr::DtogRx;
		uint16 status = ep.txStalled ? USBFsdevEpr::TxStall : USBFsdevEpr::TxValid;
		uint16 mask = static_cast<uint16>(USBFsdevEpr::DtogTx | USBFsdevEpr::DtogRx | USBFsdevEpr::TxStatus);
		EprWrite(number, 0, mask, static_cast<uint16>(select | release | status));
	}


	// True for an open isochronous endpoint. Its two directions share the type, so either one shows it.
	bool IsIsochronous(uint8 number) const {
		const EndpointState& ep = endpoints[number];
		return ep.type == USBEndpointType::Isochronous && (ep.txOpen || ep.rxOpen);
	}


	// Isochronous IN, one packet for the next token. The hardware sends the buffer DTOG_TX selects, and the completion of the packet before
	// toggled DTOG_TX onto the buffer that was not sent, so that is the buffer this copy fills. The COUNT is set with the copy. No EPnR bit
	// is written: STAT stays VALID from open, and SW_BUF is not used for an isochronous endpoint. A token that finds COUNT 0 in the buffer
	// it selects gets a zero-length packet (HandleIsoInCompletion()). The caller must not call this while a token of this endpoint can
	// be sending the selected buffer: it runs from the completion callback (the token has finished) or from HandleIsoInCompletion().
	void ArmIsoIn(uint8 number, std::span<const uint8> data) {
		bool buffer1 = (ReadEpr(number) & USBFsdevEpr::DtogTx) != 0;
		FillInBuffer(number, buffer1, data);
	}


	// Both buffers of an isochronous IN endpoint are emptied. A token after this sends a zero-length packet, never a packet armed before
	// (the COUNT of a sent buffer is zeroed by HandleIsoInCompletion(), and this covers the ones that were armed and not sent).
	void ZeroIsoInCounts(uint8 number) {
		WriteBd(number, USBFsdevBuffer::CountTx, 0);
		WriteBd(number, USBFsdevBuffer::CountRx, 0);
	}


	// Isochronous IN, a completion (CTR_TX). The hardware sent the buffer DTOG_TX selected before its toggle, so the buffer just sent is the one
	// that DTOG_TX no longer selects in the value read here. Its COUNT is zeroed, so a later token with nothing armed sends a zero-length packet
	// and never this packet again. CTR_TX is cleared and the toggles are left as they are. With no transfer active (the zero-length packet of a
	// frame nobody armed) the completion only does that, and raises no event.
	//
	// A packet armed from outside the completion callback is deferred (StartInTransfer()): the token that finds it not yet written sent a
	// zero-length packet, and the packet is written here into the buffer DTOG_TX now selects, which is free. It goes out at the next token and
	// completes then. Nothing completes at this token. That is why the arm never races a token: no copy runs while a token can read the buffer.
	// Otherwise the transfer completes with the packet that was sent. The completion callback may arm the next packet, which goes out at the
	// next token, so the endpoint streams one packet per frame (USBDeviceAdapter.h, "Isochronous endpoints").
	//
	// This relies on the interrupt being served within one frame of the token. A later service merges two tokens' completions, and the buffer
	// that went out cannot be told apart from the one after it.
	void HandleIsoInCompletion(uint8 number) {
		EndpointState& ep = endpoints[number];
		uint16 epr = ReadEpr(number);
		bool sentBuffer1 = (epr & USBFsdevEpr::DtogTx) == 0;
		EprWrite(number, USBFsdevEpr::CtrTx, 0, 0);
		WriteBd(number, sentBuffer1 ? USBFsdevBuffer::CountRx : USBFsdevBuffer::CountTx, 0);
		if (!ep.txActive) {
			return;
		}

		if (ep.txDeferred) {
			ep.txDeferred = false;
			ArmIsoIn(number, ep.txData);
			return;
		}

		ep.txCompleting = true;
		FinishIn(number);
		ep.txCompleting = false;
	}


	// Isochronous OUT, a completion (CTR_RX). The hardware stored the packet in the buffer DTOG_RX selected before its toggle, so the buffer just
	// filled is the one that DTOG_RX no longer selects in the value read here, and its COUNT is the packet length. The hardware toggles DTOG_RX
	// for the next frame by itself. The write also clears CTR_RX and toggles DTOG_TX (see below). A packet that arrives with no transfer armed is dropped with no event
	// and no copy, because a stale frame must not be delivered as if it came now. The packet is not held for a later transfer. A packet longer
	// than its block cannot have been stored whole, and one that does not fit the buffer is dropped too; both are dropped whole, with DataOverrun,
	// and the transfer completes with the bytes stored before it, which is 0 for a one-packet transfer.
	void HandleIsoOutCompletion(uint8 number) {
		EndpointState& ep = endpoints[number];
		uint16 epr = ReadEpr(number);
		bool received1 = (epr & USBFsdevEpr::DtogRx) == 0;
		uint32 count = ReadBd(number, received1 ? USBFsdevBuffer::CountRx : USBFsdevBuffer::CountTx) & USBFsdevBuffer::CountMask;
		// ST toggles DTOG_TX once per isochronous OUT completion (PCD_FREE_USER_BUFFER, stm32g4xx_hal_pcd.c lines 1785-1800). The
		// isochronous model says the controller needs no such toggle for OUT. It is kept in case that model is wrong for OUT, and it
		// is the only change this write makes: CTR_RX is cleared, CTR_TX and DTOG_RX are written as they are. A number carries one
		// isochronous direction, so the toggle moves no IN buffer select.
		uint16 toggleTx = (epr & USBFsdevEpr::DtogTx) != 0 ? static_cast<uint16>(0) : USBFsdevEpr::DtogTx;
		EprWrite(number, USBFsdevEpr::CtrRx, USBFsdevEpr::DtogTx, toggleTx);
		if (!ep.rxActive) {
			return;
		}

		uint32 block = received1 ? ep.sizeRx : ep.sizeTx;
		uint32 room = static_cast<uint32>(ep.rxBuffer.size()) - ep.rxDone;
		if (count > block || count > room) {
			CallError(Error::DataOverrun);
			FinishOut(number);
			return;
		}

		CopyFromPma(received1 ? ep.pmaRx : ep.pmaTx, ep.rxBuffer.subspan(0, count));
		ep.rxDone = count;
		FinishOut(number);
	}


	// Takes both buffers back from the hardware. DTOG_RX equal to DTOG_TX is a buffer conflict, which the hardware NAKs.
	void TakeBackIn(uint8 number) {
		uint16 epr = ReadEpr(number);
		uint16 takeBack = (epr & USBFsdevEpr::DtogTx) != 0 ? USBFsdevEpr::DtogRx : static_cast<uint16>(0);
		EprWrite(number, 0, USBFsdevEpr::DtogRx, takeBack);
	}


	// Double-buffered endpoint, OUT: the same with the bits exchanged. The hand-back gives buffer DTOG_RX to the hardware by
	// making DTOG_TX differ from it. SW_BUF (DTOG_TX) is written first, on its own. The STAT write that follows takes its base from
	// the software copies (KnownEpr()), so the endpoint is not reset to a stale status.
	void ArmOut(uint8 number) {
		EndpointState& ep = endpoints[number];
		uint16 status = ep.rxStalled ? USBFsdevEpr::RxStall : USBFsdevEpr::RxValid;
		if (!ep.doubleBuffer) {
			EprWrite(number, 0, USBFsdevEpr::RxStatus, status);
			return;
		}

		uint16 epr = ReadEpr(number);
		uint16 handBack = (epr & USBFsdevEpr::DtogRx) != 0 ? static_cast<uint16>(0) : USBFsdevEpr::DtogTx;
		EprWrite(number, 0, USBFsdevEpr::DtogTx, handBack);
		EprWrite(number, 0, USBFsdevEpr::RxStatus, status);
	}


	// ClearStall() of a double-buffered IN endpoint. Both buffers are taken back first, so the hardware NAKs and starts no packet. Only then
	// is CTR read, so a completed packet is taken (it may end the transfer) on a state the hardware cannot change any more. An idle endpoint
	// gets DATA0 and NAK. An active transfer restarts from the first packet the host has not acknowledged (txConfirmed): both buffers are
	// filled again, buffer 0 first, and DATA0 is sent from buffer 0. A release made by the completion is taken back again before the refill.
	// No transaction of this endpoint may be on the wire while this runs: the host sends CLEAR_FEATURE as a control transfer, so no bulk
	// transaction is on the bus at that point.
	void ClearDoubleBufferIn(uint8 number) {
		EndpointState& ep = endpoints[number];
		TakeBackIn(number);
		if (ep.txActive && (ReadEpr(number) & USBFsdevEpr::CtrTx) != 0) {
			HandleInCompletion(number);
			TakeBackIn(number);
		}

		if (!ep.txActive) {
			ep.txHasPrefilled = false;
			EprWrite(number, 0, static_cast<uint16>(USBFsdevEpr::DtogTx | USBFsdevEpr::DtogRx), 0);
			EprWrite(number, 0, USBFsdevEpr::TxStatus, USBFsdevEpr::TxNak);
			return;
		}

		ep.txQueued = ep.txConfirmed;
		ep.txZeroLengthQueued = false;
		LoadDoubleBufferIn(number, false);
	}


	// ClearStall() of a double-buffered OUT endpoint. The armed buffer is taken back first: DTOG_TX equal to DTOG_RX is a buffer
	// conflict, so the hardware NAKs and stores no packet. Only then is CTR read, and a completed packet still pending is taken.
	// A packet taken while no transfer is active is held in the buffer it is in, so the toggle reset below does not lose its place.
	// An idle endpoint gets DATA0 and NAK. An active transfer gives buffer 0 to the hardware: DTOG_RX 0 selects buffer 0 with
	// DATA0, and DTOG_TX 1 (SW_BUF) hands it over. The precondition of ClearDoubleBufferIn() applies here too.
	void ClearDoubleBufferOut(uint8 number) {
		EndpointState& ep = endpoints[number];
		uint16 epr = ReadEpr(number);
		uint16 takeBack = (epr & USBFsdevEpr::DtogRx) != 0 ? USBFsdevEpr::DtogTx : static_cast<uint16>(0);
		EprWrite(number, 0, USBFsdevEpr::DtogTx, takeBack);
		if ((ReadEpr(number) & USBFsdevEpr::CtrRx) != 0) {
			HandleOutCompletion(number);
		}

		if (!ep.rxActive) {
			EprWrite(number, 0, static_cast<uint16>(USBFsdevEpr::DtogRx | USBFsdevEpr::DtogTx), 0);
			EprWrite(number, 0, USBFsdevEpr::RxStatus, USBFsdevEpr::RxNak);
			return;
		}

		EprWrite(number, 0, static_cast<uint16>(USBFsdevEpr::DtogRx | USBFsdevEpr::DtogTx), USBFsdevEpr::DtogTx);
		uint16 status = ep.rxStalled ? USBFsdevEpr::RxStall : USBFsdevEpr::RxValid;
		EprWrite(number, 0, USBFsdevEpr::RxStatus, status);
	}


	// The next packet of an IN transfer, in the order the host reads them: the data in packet-size pieces, then the zero-length packet
	// when the transfer needs one. Returns false when nothing is left, and the caller completes the transfer.
	bool NextInChunk(uint8 number, std::span<const uint8>& chunk) {
		EndpointState& ep = endpoints[number];
		uint32 packetSize = ep.txPacketSize;
		if (ep.txQueued < ep.txSize) {
			uint32 remaining = ep.txSize - ep.txQueued;
			uint16 length = static_cast<uint16>(remaining < packetSize ? remaining : packetSize);
			chunk = ep.txData.subspan(ep.txQueued, length);
			ep.txQueued += length;
			return true;
		}

		bool zeroLengthNeeded = ep.txSize == 0 || (ep.txZeroLengthRequested && ep.txSize % packetSize == 0);
		if (zeroLengthNeeded && !ep.txZeroLengthQueued) {
			ep.txZeroLengthQueued = true;
			chunk = std::span<const uint8>();
			return true;
		}

		return false;
	}


	// Arms the next packet of a single-buffered IN transfer, or the zero-length packet when the transfer needs one. Returns false when
	// nothing is left: the caller completes the transfer.
	bool ArmNextIn(uint8 number) {
		std::span<const uint8> chunk;
		if (!NextInChunk(number, chunk)) {
			return false;
		}

		ArmIn(number, chunk);
		return true;
	}


	void FinishIn(uint8 number) {
		EndpointState& ep = endpoints[number];
		ep.txActive = false;
		// An isochronous endpoint has no NAK, so its STAT stays VALID from open (see ArmIsoIn()).
		if (ep.type != USBEndpointType::Isochronous) {
			EprWrite(number, 0, USBFsdevEpr::TxStatus, USBFsdevEpr::TxNak);
		}

		if (number == 0 && pendingAddressValid) {
			WriteReg(USBFsdevRegister::Daddr, static_cast<uint16>(USBFsdevDaddr::Ef | pendingAddress));
			pendingAddressValid = false;
		}

		CallTransferComplete(USBEndpoint::MakeIn(number), ep.txSize);
	}


	// A double-buffered endpoint keeps its STAT valid. The buffer conflict is what NAKs the next packet, so a packet that arrives after
	// the transfer is held for the next one (HandleDoubleBufferOutCompletion()). The HAL sets the STAT to NAK here instead
	// (HAL_PCD_EP_DB_Receive() in the G4 HAL), which would stop that packet.
	void FinishOut(uint8 number) {
		EndpointState& ep = endpoints[number];
		ep.rxActive = false;
		if (!ep.doubleBuffer) {
			EprWrite(number, 0, USBFsdevEpr::RxStatus, USBFsdevEpr::RxNak);
		}

		CallTransferComplete(USBEndpoint::MakeOut(number), ep.rxDone);
	}


	// True for the completion the fast path serves: CTR_TX of a double-buffered IN endpoint that is active and has a prefilled buffer, with
	// none of the flags that change the endpoints set. The ISTR fields are the read at entry, so no register is read to decide.
	bool IsFastInCompletion(uint16 istr) const {
		uint16 orderedFlags = static_cast<uint16>(USBFsdevIstr::Reset | USBFsdevIstr::Susp | USBFsdevIstr::Wkup | USBFsdevIstr::Err | USBFsdevIstr::PmaOvr);
		if ((istr & orderedFlags) != 0 || (istr & USBFsdevIstr::Ctr) == 0 || (istr & USBFsdevIstr::Dir) != 0) {
			return false;
		}

		uint8 number = static_cast<uint8>(istr & USBFsdevIstr::EpId);
		if (number >= Family::HardwareEndpointCount) {
			return false;
		}

		const EndpointState& ep = endpoints[number];
		return ep.doubleBuffer && ep.txActive && ep.txHasPrefilled;
	}


	// The fast path's service: one EPnR read, then the release write of HandleDoubleBufferInCompletion(). The CTR_TX check guards the case
	// where the ISTR fields and the endpoint disagree; the loop then serves whatever the controller shows.
	void ServeFastInCompletion(uint8 number) {
		uint16 epr = ReadEpr(number);
		if ((epr & USBFsdevEpr::CtrTx) == 0) {
			return;
		}

		HandleDoubleBufferInCompletion(number, epr);
	}


	// A completion of an IN direction that is not open is only acknowledged. The flag may be left from before a close, and the endpoint
	// number may now be an OUT, whose COUNTs and toggles must not be touched by an IN handler.
	void HandleInCompletion(uint8 number) {
		if (!endpoints[number].txOpen) {
			EprWrite(number, USBFsdevEpr::CtrTx, 0, 0);
			return;
		}

		if (endpoints[number].type == USBEndpointType::Isochronous) {
			HandleIsoInCompletion(number);
			return;
		}

		if (endpoints[number].doubleBuffer) {
			HandleDoubleBufferInCompletion(number, ReadEpr(number));
			return;
		}

		EprWrite(number, USBFsdevEpr::CtrTx, 0, 0);
		if (!endpoints[number].txActive) {
			return;
		}

		if (!ArmNextIn(number)) {
			FinishIn(number);
		}
	}


	// Double-buffered IN, a completion, given the EPnR value the caller has read (epr). The hardware sent the buffer DTOG_TX pointed to before
	// the ACK, and DTOG_TX now points to the buffer that holds the prefilled packet. The write that clears CTR_TX also releases the prefilled
	// buffer, and it comes before the copy into the freed buffer, so a host token that arrives during that copy is ACKed. With nothing
	// prefilled, the completion ends the transfer: it is the completion of the last packet, and the transfer completes after that packet was sent.
	// The release is built from epr, so it reads no register: DTOG_RX is the only toggle, and the STAT bits keep their write bits at 0.
	void HandleDoubleBufferInCompletion(uint8 number, uint16 epr) {
		EndpointState& ep = endpoints[number];
		uint16 known = KnownEpr(number, epr);
		uint16 stableBits = static_cast<uint16>(epr & USBFsdevEpr::Stable);
		if (!ep.txActive) {
			WriteEprKnown(number, known, stableBits, USBFsdevEpr::CtrTx, 0, 0);
			return;
		}

		bool prefilledIsBuffer1 = (epr & USBFsdevEpr::DtogTx) != 0;
		ep.txConfirmed += static_cast<uint32>(ep.txArmed.size());
		if (!ep.txHasPrefilled) {
			WriteEprKnown(number, known, stableBits, USBFsdevEpr::CtrTx, 0, 0);
			FinishIn(number);
			return;
		}

		// The release of the prefilled buffer: SW_BUF takes the value that differs from DTOG_TX, as in LoadDoubleBufferIn().
		uint16 release = prefilledIsBuffer1 ? static_cast<uint16>(0) : USBFsdevEpr::DtogRx;
		WriteEprKnown(number, known, stableBits, USBFsdevEpr::CtrTx, USBFsdevEpr::DtogRx, release);
		ep.txArmed = ep.txPrefilled;
		ep.txHasPrefilled = false;
		std::span<const uint8> next;
		if (NextInChunk(number, next)) {
			// The buffer just sent is the other one, and it is free now.
			FillInBuffer(number, !prefilledIsBuffer1, next);
			ep.txPrefilled = next;
			ep.txHasPrefilled = true;
		}
	}


	// A completion of an OUT direction that is not open is only acknowledged. A closed OUT must not hold a packet for a later transfer, and an
	// isochronous one must not toggle DTOG_TX, which is the buffer select of the IN when the number is open as an IN.
	void HandleOutCompletion(uint8 number) {
		if (!endpoints[number].rxOpen) {
			EprWrite(number, USBFsdevEpr::CtrRx, 0, 0);
			return;
		}

		if (endpoints[number].doubleBuffer) {
			HandleDoubleBufferOutCompletion(number);
			return;
		}

		EprWrite(number, USBFsdevEpr::CtrRx, 0, 0);
		EndpointState& ep = endpoints[number];
		if (!ep.rxActive) {
			return;
		}

		uint32 count = ReadBd(number, USBFsdevBuffer::CountRx) & USBFsdevBuffer::CountMask;
		uint32 room = static_cast<uint32>(ep.rxBuffer.size()) - ep.rxDone;
		if (count > room) {
			CallError(Error::DataOverrun);
			FinishOut(number);
			return;
		}

		CopyFromPma(ep.pmaRx, ep.rxBuffer.subspan(ep.rxDone, count));
		ep.rxDone += count;
		bool complete = count < ep.rxPacketSize || ep.rxDone == ep.rxBuffer.size();
		if (complete) {
			FinishOut(number);
			return;
		}

		ArmOut(number);
	}


	// Double-buffered OUT, a completion. After the ACK the hardware filled buffer !DTOG_RX, and DTOG_RX now points to the buffer it fills next.
	// That next buffer goes back to the hardware in the same write that clears CTR_RX (DTOG_TX takes the value that differs from DTOG_RX), and
	// the copy out of the packet follows, so a packet that arrives during the copy is accepted. With no transfer active the packet is held: the
	// next buffer is not handed back, so the endpoint NAKs until StartOutTransfer(). The overrun is checked before the hand-back, so a
	// refused packet is never released to the hardware.
	void HandleDoubleBufferOutCompletion(uint8 number) {
		EndpointState& ep = endpoints[number];
		if (ep.type == USBEndpointType::Isochronous) {
			HandleIsoOutCompletion(number);
			return;
		}

		uint16 epr = ReadEpr(number);
		bool received1 = (epr & USBFsdevEpr::DtogRx) == 0;
		uint32 count = ReadBd(number, received1 ? USBFsdevBuffer::CountRx : USBFsdevBuffer::CountTx) & USBFsdevBuffer::CountMask;
		if (!ep.rxActive) {
			EprWrite(number, USBFsdevEpr::CtrRx, 0, 0);
			ep.rxHeld = true;
			ep.rxHeldBuffer1 = received1;
			ep.rxHeldCount = count;
			return;
		}

		uint32 room = static_cast<uint32>(ep.rxBuffer.size()) - ep.rxDone;
		if (count > room) {
			EprWrite(number, USBFsdevEpr::CtrRx, 0, 0);
			CallError(Error::DataOverrun);
			FinishOut(number);
			return;
		}

		uint16 handBack = (epr & USBFsdevEpr::DtogRx) != 0 ? static_cast<uint16>(0) : USBFsdevEpr::DtogTx;
		EprWrite(number, USBFsdevEpr::CtrRx, USBFsdevEpr::DtogTx, handBack);
		CopyFromPma(received1 ? ep.pmaRx : ep.pmaTx, ep.rxBuffer.subspan(ep.rxDone, count));
		ep.rxDone += count;
		bool complete = count < ep.rxPacketSize || ep.rxDone == ep.rxBuffer.size();
		if (complete) {
			FinishOut(number);
		}
	}


	// Copies the packet that arrived before the transfer started into the new transfer. Returns true when that packet ended the transfer.
	// The completion is then reported from here, before StartOutTransfer() returns. The held buffer is free after the copy; the other buffer
	// is handed back by the caller when the transfer goes on (ArmOut()).
	bool DeliverHeldOut(uint8 number) {
		EndpointState& ep = endpoints[number];
		ep.rxHeld = false;
		uint32 count = ep.rxHeldCount;
		if (count > ep.rxBuffer.size()) {
			CallError(Error::DataOverrun);
			FinishOut(number);
			return true;
		}

		CopyFromPma(ep.rxHeldBuffer1 ? ep.pmaRx : ep.pmaTx, ep.rxBuffer.subspan(0, count));
		ep.rxDone = count;
		bool complete = count < ep.rxPacketSize || ep.rxDone == ep.rxBuffer.size();
		if (complete) {
			FinishOut(number);
		}

		return complete;
	}


	uint16 ReadBd(uint8 number, uint32 field) const {
		return ReadPma(USBFsdevBuffer::Table + number * USBFsdevBuffer::EntrySize + field);
	}


	// A SETUP abandons the endpoint 0 transfers without events and clears the endpoint 0 halt. A completion the hardware
	// finished before the SETUP is reported first; the rest are dropped.
	void HandleSetup() {
		// Copy the SETUP before the pending IN completion runs: its callback can arm endpoint 0 OUT, and the next host packet
		// would then overwrite the SETUP bytes in the buffer.
		USBSetupBytes setup;
		CopyFromPma(endpoints[0].pmaRx, std::span<uint8>(setup.bytes, USBSetupBytes::Size));
		if ((ReadEpr(0) & USBFsdevEpr::CtrTx) != 0) {
			HandleInCompletion(0);
		}

		EndpointState& ep0 = endpoints[0];
		ep0.txActive = false;
		ep0.rxActive = false;
		ep0.txStalled = false;
		ep0.rxStalled = false;
		pendingAddressValid = false;
		uint16 statusBits = static_cast<uint16>(USBFsdevEpr::TxStatus | USBFsdevEpr::RxStatus);
		uint16 nakBits = static_cast<uint16>(USBFsdevEpr::TxNak | USBFsdevEpr::RxNak);
		EprWrite(0, USBFsdevEpr::CtrRx, statusBits, nakBits);
		CallSetup(setup);
	}


	// Serves one endpoint direction with a pending transfer flag. Returns false when no flag is set.
	bool HandleCorrectTransfer() {
		uint16 istr = ReadIstr();
		uint8 number = static_cast<uint8>(istr & USBFsdevIstr::EpId);
		bool inDirection = (istr & USBFsdevIstr::Dir) == 0;
		uint16 epr = ReadEpr(number);
		bool txPending = (epr & USBFsdevEpr::CtrTx) != 0;
		bool rxPending = (epr & USBFsdevEpr::CtrRx) != 0;
		if (inDirection && txPending) {
			HandleInCompletion(number);
			return true;
		}

		if (rxPending) {
			if ((epr & USBFsdevEpr::Setup) != 0) {
				HandleSetup();
			} else {
				HandleOutCompletion(number);
			}

			return true;
		}

		if (txPending) {
			HandleInCompletion(number);
			return true;
		}

		return false;
	}


	void HandleBusReset() {
		CloseAllEndpoints();
		EndpointState& ep0 = endpoints[0];
		ep0.txOpen = true;
		ep0.rxOpen = true;
		ep0.txActive = false;
		ep0.rxActive = false;
		ep0.txStalled = false;
		ep0.rxStalled = false;
		ep0.doubleBuffer = false;
		ep0.type = USBEndpointType::Control;
		ep0.txPacketSize = ControlMaxPacketSize;
		ep0.rxPacketSize = ControlMaxPacketSize;
		pendingAddressValid = false;
		suspended = false;
		resumeTicksLeft = 0;
		uint16 resumeBits = static_cast<uint16>(USBFsdevCntr::Resume | USBFsdevCntr::EsofM);
		uint16 lowPowerBits = static_cast<uint16>(USBFsdevCntr::FSusp | USBFsdevCntr::LpMode);
		SetCntrBits(0, static_cast<uint16>(resumeBits | lowPowerBits));
		WriteReg(USBFsdevRegister::Daddr, USBFsdevDaddr::Ef);
		WriteBd(0, USBFsdevBuffer::AddrTx, ep0.pmaTx);
		WriteBd(0, USBFsdevBuffer::CountTx, 0);
		WriteBd(0, USBFsdevBuffer::AddrRx, ep0.pmaRx);
		WriteBd(0, USBFsdevBuffer::CountRx, EncodeReceiveCount(ControlMaxPacketSize));
		uint16 statusBits = static_cast<uint16>(USBFsdevEpr::TxStatus | USBFsdevEpr::RxStatus);
		uint16 dtogBits = static_cast<uint16>(USBFsdevEpr::DtogTx | USBFsdevEpr::DtogRx);
		uint16 nakBits = static_cast<uint16>(USBFsdevEpr::TxNak | USBFsdevEpr::RxNak);
		EprWriteStable(0, USBFsdevEpr::TypeControl, 0, static_cast<uint16>(statusBits | dtogBits), nakBits);
		CallBusReset(USBSpeed::Full);
	}


	// The controller expects FSUSP set before the SUSP flag is cleared, so the flag is cleared here and not in IrqHandler().
	// (stm32g4xx_hal_pcd.c, HAL_PCD_IRQHandler(), SUSP branch, lines 997-1005: FSUSP is set at line 1000 before the flag is
	// cleared at line 1003.)
	void HandleSuspend() {
		suspended = true;
		SetCntrBits(static_cast<uint16>(USBFsdevCntr::FSusp | USBFsdevCntr::LpMode), 0);
		ClearIstr(USBFsdevIstr::Susp);
		CallSuspend();
	}


	void HandleWakeup() {
		SetCntrBits(0, static_cast<uint16>(USBFsdevCntr::FSusp | USBFsdevCntr::LpMode));
		if (resumeTicksLeft != 0) {
			resumeTicksLeft = 0;
			SetCntrBits(0, static_cast<uint16>(USBFsdevCntr::Resume | USBFsdevCntr::EsofM));
		}

		suspended = false;
		CallResume();
	}


	void HandleExpectedSof() {
		if (resumeTicksLeft == 0) {
			return;
		}

		resumeTicksLeft--;
		if (resumeTicksLeft == 0) {
			SetCntrBits(0, static_cast<uint16>(USBFsdevCntr::Resume | USBFsdevCntr::EsofM));
			suspended = false;
			CallResume();
		}
	}
};
