#pragma once
#include <VHAL.h>
#include <Adapter/Port/STM32/Common/USBFsdev.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>


// The answer of the controller to one token, as the host sees it. None is no answer at all (a disabled endpoint).
enum class SimHandshake : uint8 {
	None,
	Ack,
	Nak,
	Stall
};


// The family the port is instantiated with. The register hooks forward to the simulated controller, and PmaBase is the packet
// memory of that controller (set by SimFsdevController::Reset()).
struct SimFsdevFamily {
	static inline uintptr_t PmaBase = 0;
	static constexpr uint32 PmaSize = 1024;
	static constexpr uint32 PmaElementStride = 2;
	static constexpr uint8 HardwareEndpointCount = 8;


	static bool IsClockEnabled() {
		return true;
	}


	static uint16 ReadRegister(uintptr_t address);
	static void WriteRegister(uintptr_t address, uint16 value);
};


// Software model of the fsdev block for the host tests on the PC. The port reaches it through the register hooks of
// SimFsdevFamily, so every EPnR write is applied with the controller's own rules, and every read shows what the controller
// shows. The packet memory is the RAM the port uses; the buffer table is read and written by both sides.
//
// The rules are those of the USB chapter of RM0440 (STM32G4) and of the HAL sources in Periphery/Adapter/Port/STM32/G4/Drivers/HAL/Src/
// stm32g4xx_hal_pcd.c and Periphery/Adapter/Port/STM32/G4/Drivers/HAL/Inc/stm32g4xx_hal_pcd.h. The comment at each rule names its source.
class SimFsdevController {
public:
	// The peripheral base the port is given. Only offsets are added to it; nothing is read from this address.
	static constexpr uintptr_t RegisterBase = 0x40005C00;
	static constexpr uint8 EndpointCount = 8;
	static constexpr uint32 PacketMemorySize = 1024;

	// Packet memory, shared with the port through SimFsdevFamily::PmaBase. Its contents are not reset by the port.
	alignas(4) uint8 packetMemory[PacketMemorySize] = {};
	// OUT packets the controller ACKed and dropped because their PID was not the expected one.
	uint32 discardedOutPackets = 0;
	// Register accesses the port has made since ResetAccessCounts(). Reads and writes are counted apart, so a test can count the
	// accesses the port makes before one of its writes.
	uint32 registerReads = 0;
	uint32 registerWrites = 0;
	// Test hook, null in normal use. Runs at the start of each EPnR write, before the write is applied, so a test can let a token
	// complete between the port's read of EPnR and its write.
	void (*beforeEprWrite)() = nullptr;
	// Test hook, null in normal use. Runs after an EPnR write is applied. The port spends time between two of its register accesses
	// (the copy of a packet, for one), and a test uses this hook to let a host token arrive in that time.
	void (*afterEprWrite)() = nullptr;


private:
	static constexpr uint32 EndpointRegisterEnd = 4U * EndpointCount;
	static constexpr uint16 EprMaskToggle = static_cast<uint16>(USBFsdevEpr::DtogRx | USBFsdevEpr::DtogTx | USBFsdevEpr::RxStatus | USBFsdevEpr::TxStatus);
	static constexpr uint16 EprMaskCtr = static_cast<uint16>(USBFsdevEpr::CtrRx | USBFsdevEpr::CtrTx);
	static constexpr uint16 EprMaskStatus = static_cast<uint16>(USBFsdevEpr::RxStatus | USBFsdevEpr::TxStatus);

	// The EPnR values as the hardware holds them. The STAT bits here are the real ones, never the masked read.
	uint16 epr[EndpointCount] = {};
	// The flags of ISTR that software clears (reset, suspend, and so on). CTR, EP_ID and DIR are computed from the EPnR.
	uint16 istrFlags = 0;
	uint16 cntr = 0;
	uint16 daddr = 0;
	uint16 btable = 0;
	uint16 bcdr = 0;


public:
	// Empties the controller and the packet memory. The port is created after this, so its registers start from here.
	void Reset() {
		std::memset(packetMemory, 0, sizeof(packetMemory));
		std::memset(epr, 0, sizeof(epr));
		istrFlags = 0;
		cntr = 0;
		daddr = 0;
		btable = 0;
		bcdr = 0;
		discardedOutPackets = 0;
		beforeEprWrite = nullptr;
		afterEprWrite = nullptr;
		ResetAccessCounts();
		SimFsdevFamily::PmaBase = reinterpret_cast<uintptr_t>(packetMemory);
	}


	// Clears the register access counts, so a test counts from the point it chooses.
	void ResetAccessCounts() {
		registerReads = 0;
		registerWrites = 0;
	}


	// The controller raises ISTR flags by itself: RESET on a bus reset (RM0440 ISTR; the HAL serves it in HAL_PCD_IRQHandler(),
	// stm32g4xx_hal_pcd.c). A test raises them here and the port's IrqHandler() serves them, as it does on the hardware.
	void RaiseIstrFlags(uint16 flags) {
		istrFlags = static_cast<uint16>(istrFlags | flags);
	}


	// The DATA PID the next packet on an address carries: DTOG_TX for IN, DTOG_RX for OUT (true is DATA1).
	bool GetDataToggle(uint8 address) const {
		uint8 number = USBEndpoint::Number(address);
		if (USBEndpoint::IsIn(address)) {
			return (epr[number] & USBFsdevEpr::DtogTx) != 0;
		}

		return (epr[number] & USBFsdevEpr::DtogRx) != 0;
	}


	// One IN token from the host. The controller sends the packet of the buffer it owns. In double-buffer mode that is buffer
	// DTOG_TX (ADDR_RX for buffer 1). The PID is DTOG_TX in both modes (RM0440: the data toggle of a double-buffered endpoint
	// selects its buffer). The host always ACKs, so the ACK effects are applied here: CTR_TX is set, DTOG_TX toggles, and a
	// single-buffered endpoint goes to NAK. packet must hold a whole maximum packet.
	SimHandshake InToken(uint8 number, std::span<uint8> packet, uint32& length, bool& dataOne) {
		length = 0;
		dataOne = false;
		uint16 value = epr[number];
		uint16 status = static_cast<uint16>(value & USBFsdevEpr::TxStatus);
		if (status == USBFsdevEpr::TxStall) {
			return SimHandshake::Stall;
		}

		if (status == USBFsdevEpr::TxDisabled) {
			return SimHandshake::None;
		}

		bool doubleBuffer = IsDoubleBuffered(value);
		// The hardware NAKs while the software owns the buffer: DTOG_TX equal to SW_BUF (DTOG_RX) is the buffer conflict.
		if (status == USBFsdevEpr::TxNak || (doubleBuffer && IsConflict(value))) {
			return SimHandshake::Nak;
		}

		bool buffer1 = doubleBuffer && (value & USBFsdevEpr::DtogTx) != 0;
		uint16 address = ReadBdWord(number, buffer1 ? USBFsdevBuffer::AddrRx : USBFsdevBuffer::AddrTx);
		uint16 countField = ReadBdWord(number, buffer1 ? USBFsdevBuffer::CountRx : USBFsdevBuffer::CountTx);
		uint32 count = countField & USBFsdevBuffer::CountMask;
		if (count != 0) {
			std::memcpy(packet.data(), packetMemory + address, count);
		}

		length = count;
		dataOne = (value & USBFsdevEpr::DtogTx) != 0;
		epr[number] = static_cast<uint16>(epr[number] ^ USBFsdevEpr::DtogTx);
		epr[number] = static_cast<uint16>(epr[number] | USBFsdevEpr::CtrTx);
		if (!doubleBuffer) {
			SetStatus(number, USBFsdevEpr::TxStatus, USBFsdevEpr::TxNak);
		}

		return SimHandshake::Ack;
	}


	// One OUT packet from the host, with the PID the host sends. The controller stores the packet in the buffer of DTOG_RX and
	// accepts it only when the PID is DTOG_RX (USB 2.0 section 8.6.4). A packet with another PID is ACKed and discarded. In
	// double-buffer mode buffer 0 is ADDR_TX (DTOG_RX 0) and buffer 1 is ADDR_RX (DTOG_RX 1); a single-buffered endpoint uses
	// ADDR_RX. After an accepted packet CTR_RX is set and DTOG_RX toggles; a single-buffered endpoint goes to NAK.
	SimHandshake OutToken(uint8 number, std::span<const uint8> data, bool dataOne) {
		uint16 value = epr[number];
		uint16 status = static_cast<uint16>(value & USBFsdevEpr::RxStatus);
		if (status == USBFsdevEpr::RxStall) {
			return SimHandshake::Stall;
		}

		if (status == USBFsdevEpr::RxDisabled) {
			return SimHandshake::None;
		}

		bool doubleBuffer = IsDoubleBuffered(value);
		if (status == USBFsdevEpr::RxNak || (doubleBuffer && IsConflict(value))) {
			return SimHandshake::Nak;
		}

		bool buffer0 = doubleBuffer && (value & USBFsdevEpr::DtogRx) == 0;
		uint16 address = ReadBdWord(number, buffer0 ? USBFsdevBuffer::AddrTx : USBFsdevBuffer::AddrRx);
		uint16 countField = ReadBdWord(number, buffer0 ? USBFsdevBuffer::CountTx : USBFsdevBuffer::CountRx);
		if (dataOne != ((value & USBFsdevEpr::DtogRx) != 0)) {
			discardedOutPackets++;
			return SimHandshake::Ack;
		}

		if (!data.empty()) {
			std::memcpy(packetMemory + address, data.data(), data.size());
		}

		uint16 count = static_cast<uint16>(data.size());
		WriteBdWord(number, buffer0 ? USBFsdevBuffer::CountTx : USBFsdevBuffer::CountRx, static_cast<uint16>((countField & ~USBFsdevBuffer::CountMask) | count));
		epr[number] = static_cast<uint16>(epr[number] | USBFsdevEpr::CtrRx);
		epr[number] = static_cast<uint16>(epr[number] ^ USBFsdevEpr::DtogRx);
		if (!doubleBuffer) {
			SetStatus(number, USBFsdevEpr::RxStatus, USBFsdevEpr::RxNak);
		}

		return SimHandshake::Ack;
	}


	// Register read, at the offset from RegisterBase.
	uint16 ReadRegister(uint32 offset) {
		registerReads++;
		if (offset < EndpointRegisterEnd) {
			return ReadEpr(static_cast<uint8>(offset / 4U));
		}

		if (offset == USBFsdevRegister::Cntr) {
			return cntr;
		}

		if (offset == USBFsdevRegister::Istr) {
			return ReadIstr();
		}

		if (offset == USBFsdevRegister::Daddr) {
			return daddr;
		}

		if (offset == USBFsdevRegister::Btable) {
			return btable;
		}

		if (offset == USBFsdevRegister::Bcdr) {
			return bcdr;
		}

		return 0;
	}


	// Register write, at the offset from RegisterBase.
	void WriteRegister(uint32 offset, uint16 value) {
		registerWrites++;
		if (offset < EndpointRegisterEnd) {
			WriteEpr(static_cast<uint8>(offset / 4U), value);
			return;
		}

		if (offset == USBFsdevRegister::Cntr) {
			cntr = value;
		} else if (offset == USBFsdevRegister::Istr) {
			// ISTR flags are cleared by writing 0 and kept by writing 1 (rc_w0).
			istrFlags = static_cast<uint16>(istrFlags & value);
		} else if (offset == USBFsdevRegister::Daddr) {
			daddr = value;
		} else if (offset == USBFsdevRegister::Btable) {
			btable = value;
		} else if (offset == USBFsdevRegister::Bcdr) {
			bcdr = value;
		}
	}


private:
	static bool IsDoubleBuffered(uint16 value) {
		return (value & USBFsdevEpr::Kind) != 0;
	}


	// A buffer conflict is DTOG_TX equal to SW_BUF (DTOG_RX). RM0440 names the conflict and masks the STAT reads during it.
	static bool IsConflict(uint16 value) {
		return ((value & USBFsdevEpr::DtogTx) != 0) == ((value & USBFsdevEpr::DtogRx) != 0);
	}


	// The EPnR read. RM0440: "the actual endpoint status will be masked as 10 (NAK) when a buffer conflict between the USB
	// peripheral and the application software is detected". The status the controller holds is not changed by the read.
	uint16 ReadEpr(uint8 number) const {
		uint16 value = epr[number];
		if (IsDoubleBuffered(value) && IsConflict(value)) {
			value = static_cast<uint16>((value & ~EprMaskStatus) | USBFsdevEpr::TxNak | USBFsdevEpr::RxNak);
		}

		return value;
	}


	// The EPnR write (RM0440, USB chapter). The address, type and kind are written as they are. CTR_RX and CTR_TX are cleared by
	// writing 0 and kept by writing 1 (rc_w0). DTOG and STAT are toggled by writing 1, so a write of 0 leaves them. SETUP is set by
	// the hardware only. The HAL uses the same writes (stm32g4xx_hal_pcd.h, PCD_CLEAR_RX_EP_CTR() line 637 and PCD_SET_EP_*_STATUS()
	// lines 481 and 506).
	void WriteEpr(uint8 number, uint16 value) {
		if (beforeEprWrite != nullptr) {
			beforeEprWrite();
		}

		uint16 stored = epr[number];
		uint16 stable = static_cast<uint16>(value & USBFsdevEpr::Stable);
		uint16 ctr = static_cast<uint16>(stored & value & EprMaskCtr);
		uint16 setup = static_cast<uint16>(stored & USBFsdevEpr::Setup);
		uint16 toggled = static_cast<uint16>((stored ^ (value & EprMaskToggle)) & EprMaskToggle);
		epr[number] = static_cast<uint16>(stable | ctr | setup | toggled);
		if (afterEprWrite != nullptr) {
			afterEprWrite();
		}
	}


	// Hardware status change (the controller itself, not a write from the port).
	void SetStatus(uint8 number, uint16 statusMask, uint16 status) {
		epr[number] = static_cast<uint16>((epr[number] & ~statusMask) | status);
	}


	// ISTR: CTR is set while any endpoint has a completed transfer. EP_ID is the first such endpoint, and DIR is 0 for an IN
	// completion (CTR_TX) and 1 for an OUT completion (CTR_RX), as the port reads them.
	uint16 ReadIstr() const {
		uint16 value = istrFlags;
		for (uint8 number = 0; number < EndpointCount; ++number) {
			uint16 ctr = static_cast<uint16>(epr[number] & EprMaskCtr);
			if (ctr == 0) {
				continue;
			}

			value = static_cast<uint16>(value | USBFsdevIstr::Ctr | number);
			if ((epr[number] & USBFsdevEpr::CtrTx) == 0) {
				value = static_cast<uint16>(value | USBFsdevIstr::Dir);
			}

			break;
		}

		return value;
	}


	// The buffer table entry of an endpoint: the packet memory offset of a field, which is the table base plus 8 per number.
	uint16 ReadBdWord(uint8 number, uint32 field) const {
		uint16 value = 0;
		std::memcpy(&value, packetMemory + btable + number * USBFsdevBuffer::EntrySize + field, sizeof(value));
		return value;
	}


	void WriteBdWord(uint8 number, uint32 field, uint16 value) {
		std::memcpy(packetMemory + btable + number * USBFsdevBuffer::EntrySize + field, &value, sizeof(value));
	}
};


inline SimFsdevController simFsdev;


inline uint16 SimFsdevFamily::ReadRegister(uintptr_t address) {
	return simFsdev.ReadRegister(static_cast<uint32>(address - SimFsdevController::RegisterBase));
}


inline void SimFsdevFamily::WriteRegister(uintptr_t address, uint16 value) {
	simFsdev.WriteRegister(static_cast<uint32>(address - SimFsdevController::RegisterBase), value);
}
