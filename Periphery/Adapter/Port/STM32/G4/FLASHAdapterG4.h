#pragma once
#include <cstring>
#include <Adapter/FLASHAdapter.h>


// STM32G4 FLASH adapter, register level (RM0440).
//
// Layout: 2 KB pages, page n is the n-th page counted from FLASH_BASE. G431 is one bank. Dual-bank G4 parts
// (G474 and relatives) select the layout with DBANK: DBANK=1 gives two equal banks, each half of the device flash,
// FLASH_CR.BKER picks the bank and PNB the page inside it; DBANK=0 gives one bank of every page, addressed by PNB alone.
// The "sector" of the FLASHAdapter interface is a page on this family.
//
// Programming is 64-bit double word only. WriteData() needs an 8-byte aligned address and a size that is a multiple of
// 8, so every double word is programmed exactly once and nothing outside the given size is written. Write() (16-bit)
// is not supported by the hardware.
//
// BSP must:
//  - Clock: nothing to enable. The flash interface sits on the AHB bus and has no RCC enable bit. The adapter never
//    configures the clock tree.
//  - Wait states: set FLASH->ACR.LATENCY for the SYSCLK the BSP configures (RM0440 flash wait-state table). The
//    adapter does not touch LATENCY.
//  - IRQ: none. Erase and program are polled, so the FLASH vector is not used and the adapter has no IrqHandler().
//  - Code location: while a bank is erased or programmed, a read of that bank waits until the operation ends. On the
//    single-bank G431 that is every read, so code that calls PageErase(), MassErase() or WriteData() may run from flash:
//    the CPU stalls for up to one operation, and the interrupts wait with it. Nothing is corrupted. Each operation is
//    bounded by the timeouts in this file. Run the caller from RAM only when the interrupt latency during the operation
//    matters. On dual-bank parts, read-while-write runs the code from the other bank instead.
//  - Option bytes: a written value takes effect at the next option reload. LaunchOptionBytes() forces the reload,
//    which resets the MCU.
//  - Caches: each erase and program disables the instruction and data cache, resets them afterwards and restores
//    the previous enable state, as RM0440 requires for flash operations.
//  - Time base: System::GetMs() must run, it bounds every busy wait.


using AFLASH = class FLASHAdapterG4;


class FLASHAdapterG4: public FLASHAdapter<FLASH_TypeDef> {
private:
	static constexpr uint32 FlashKey1 = 0x45670123UL;
	static constexpr uint32 FlashKey2 = 0xCDEF89ABUL;
	static constexpr uint32 OptionKey1 = 0x08192A3BUL;
	static constexpr uint32 OptionKey2 = 0x4C5D6E7FUL;
	static constexpr uint32 PageSize = 2048;
	static constexpr size_t WriteGranule = 8;
	static constexpr uint8 RdpLevel0 = 0xAA;
	static constexpr uint8 RdpLevel1 = 0x55;
	static constexpr uint8 RdpLevel2 = 0xCC;
	static constexpr uint32 ProgramTimeoutMs = 10;
	static constexpr uint32 PageEraseTimeoutMs = 100;
	static constexpr uint32 MassEraseTimeoutMs = 1000;
	static constexpr uint32 OptionTimeoutMs = 1000;
	static constexpr uint32 BusyMask = FLASH_SR_BSY;
	static constexpr uint32 ErrorMask = FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
	                                    FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR |
	                                    FLASH_SR_RDERR | FLASH_SR_OPTVERR;
	static constexpr uint32 ClearMask = ErrorMask | FLASH_SR_EOP;
	static constexpr uint32 CacheEnableMask = FLASH_ACR_ICEN | FLASH_ACR_DCEN;
	static constexpr uint32 CacheResetMask = FLASH_ACR_ICRST | FLASH_ACR_DCRST;
#if defined(FLASH_CR_BKER)
	static constexpr uint32 BankSelectMask = FLASH_CR_BKER;
	// Dual-bank parts use 8 PNB bits in single-bank mode (256 pages); the CMSIS mask only covers the dual-bank width.
	static constexpr uint32 PageNumberMask = 0xFFUL << FLASH_CR_PNB_Pos;
#else
	static constexpr uint32 BankSelectMask = 0;
	static constexpr uint32 PageNumberMask = FLASH_CR_PNB_Msk;
#endif


public:
	FLASHAdapterG4() = default;
	FLASHAdapterG4(FLASH_TypeDef *flash) : FLASHAdapter(flash) { }


	// The defaults are the RM0440 keys. The bootloader calls Unlock() without arguments through the AFLASH alias.
	virtual ResultStatus Unlock(uint32 key1 = FlashKey1, uint32 key2 = FlashKey2) override {
		if (!IsLocked()) {
			return ResultStatus::ok;
		}

		flashHandle->KEYR = key1;
		flashHandle->KEYR = key2;

		return IsLocked() ? ResultStatus::accessError : ResultStatus::ok;
	}


	virtual ResultStatus Lock() override {
		flashHandle->CR |= FLASH_CR_LOCK;
		return ResultStatus::ok;
	}


	virtual Result<uint8> Read(uint8 *address) override {
		if (!IsFlashArea(reinterpret_cast<uint32>(address), sizeof(uint8))) {
			return ResultStatus::invalidParameter;
		}

		return Ok<uint8>(*address);
	}


	// 'address' and 'size' must be multiples of 8. The bootloader's writeAlignment has to be 8 on this family (the
	// default of 4 is refused here, because a 4-byte step would program the same double word twice).
	virtual ResultStatus WriteData(uint32 *address, const void *data, size_t size) override {
		if (size == 0) {
			return ResultStatus::ok;
		}

		if (data == nullptr) {
			return ResultStatus::invalidParameter;
		}

		if (reinterpret_cast<uint32>(address) % WriteGranule != 0) {
			return ResultStatus::invalidParameter;
		}

		// A partial double word would be padded and programmed whole, which writes past 'size' and programs the same
		// double word again on the next append. Refusing it keeps every write inside the given range.
		if (size % WriteGranule != 0) {
			return ResultStatus::invalidParameter;
		}

		if (!IsFlashArea(reinterpret_cast<uint32>(address), size)) {
			return ResultStatus::invalidParameter;
		}

		if (IsLocked()) {
			return ResultStatus::accessError;
		}

		if (!WaitNotBusy(ProgramTimeoutMs)) {
			return ResultStatus::timeout;
		}

		uint32 cachesEnabled = SuspendCaches();
		ResultStatus status = ProgramBuffer(address, static_cast<const uint8 *>(data), size);
		ResetAndRestoreCaches(cachesEnabled);

		return status;
	}


	// The hardware programs double words only; a 16-bit write would need the other half of a programmed double word.
	virtual ResultStatus Write(uint16 * /*address*/, uint16 /*data*/) override {
		return ResultStatus::notSupported;
	}


	// Any byte of a page selects that page, because the hardware erases whole pages only.
	virtual ResultStatus PageErase(uint8 *address) override {
		uint32 addressValue = reinterpret_cast<uint32>(address);
		if (addressValue < FLASH_BASE) {
			return ResultStatus::invalidParameter;
		}

		return SectorErase((addressValue - FLASH_BASE) / PageSize);
	}


	// 'sectorNumber' is the page index counted from FLASH_BASE. The page must lie inside the configured region.
	virtual ResultStatus SectorErase(uint32 sectorNumber) override {
		uint64 pageStart = static_cast<uint64>(FLASH_BASE) + static_cast<uint64>(sectorNumber) * PageSize;
		if (!IsFlashArea(pageStart, PageSize)) {
			return ResultStatus::invalidParameter;
		}

		if (IsLocked()) {
			return ResultStatus::accessError;
		}

		return ErasePage(sectorNumber);
	}


	// Erases every bank of the device, including the bootloader itself. Only use it when the whole flash is expendable.
	virtual ResultStatus MassErase() override {
		if (IsLocked()) {
			return ResultStatus::accessError;
		}

		if (!WaitNotBusy(MassEraseTimeoutMs)) {
			return ResultStatus::timeout;
		}

		uint32 cachesEnabled = SuspendCaches();
		ResultStatus status = EraseBank(FLASH_CR_MER1);

#if defined(FLASH_CR_MER2)
		if (status == ResultStatus::ok && IsDualBank()) {
			status = EraseBank(FLASH_CR_MER2);
		}
#endif

		ResetAndRestoreCaches(cachesEnabled);

		return status;
	}


	// Reports the result of the last operation. Error flags stay set until the next ClearStatusFlags(), which every
	// erase and program runs first, so the result can be read after the operation returns.
	virtual ResultStatus GetStatus() override {
		uint32 sr = flashHandle->SR;

		if ((sr & BusyMask) != 0) {
			return ResultStatus::busy;
		}

		if ((sr & ErrorMask) != 0) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


	virtual ResultStatus ClearStatusFlags() override {
		// SR flags are write-1-to-clear: a plain store clears exactly the bits of ClearMask and leaves the rest alone.
		flashHandle->SR = ClearMask;
		return ResultStatus::ok;
	}


	virtual Result<uint32> ReadOptionBytes() override {
		return Ok<uint32>(static_cast<uint32>(flashHandle->OPTR));
	}


	// Writes the whole OPTR value. Read-modify-write is the caller's job; this adapter only sets RDP itself.
	virtual ResultStatus WriteOptionBytes(uint32 optionBytes) override {
		// FLASH_CR is write-protected while LOCK is set, so the OPTSTRT write would be dropped and the call would still
		// report success. Refusing before the option lock is released also leaves the option lock as it was.
		if (IsLocked()) {
			return ResultStatus::accessError;
		}

		ResultStatus status = UnlockOptionBytes();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!WaitNotBusy(OptionTimeoutMs)) {
			LockOptionBytes();
			return ResultStatus::timeout;
		}

		ClearStatusFlags();
		flashHandle->OPTR = optionBytes;
		flashHandle->CR |= FLASH_CR_OPTSTRT;

		bool done = WaitNotBusy(OptionTimeoutMs);
		LockOptionBytes();

		if (!done) {
			return ResultStatus::timeout;
		}

		return GetStatus();
	}


	virtual bool IsReadProtected() const override {
		return GetReadProtectionLevel() != RdpLevel0;
	}


	virtual uint8 GetReadProtectionLevel() const override {
		return static_cast<uint8>((flashHandle->OPTR & FLASH_OPTR_RDP) >> FLASH_OPTR_RDP_Pos);
	}


	virtual FlashProtectionLevel GetProtectionLevel() const override {
		uint8 rdp = GetReadProtectionLevel();

		if (rdp == RdpLevel0) {
			return FlashProtectionLevel::Level0;
		}

		if (rdp == RdpLevel2) {
			return FlashProtectionLevel::Level2;
		}

		// Every other RDP value is level 1 on this family
		return FlashProtectionLevel::Level1;
	}


	// Level 2 is permanent. Writing RDP takes effect at the next option reload (see LaunchOptionBytes()).
	// Writing RDP 1 -> 0 makes the hardware mass-erase the flash.
	virtual ResultStatus SetReadProtectionLevel(FlashProtectionLevel level) override {
		uint8 rdp = 0;

		switch (level) {
			case FlashProtectionLevel::Level0:
				rdp = RdpLevel0;
				break;

			case FlashProtectionLevel::Level1:
				rdp = RdpLevel1;
				break;

			case FlashProtectionLevel::Level2:
				rdp = RdpLevel2;
				break;

			case FlashProtectionLevel::Unknown:
				return ResultStatus::invalidParameter;
		}

		// The hardware refuses a downgrade from level 2. Checking first gives the caller an explicit error.
		if (GetProtectionLevel() == FlashProtectionLevel::Level2 && level != FlashProtectionLevel::Level2) {
			return ResultStatus::accessError;
		}

		uint32 optionBytes = (flashHandle->OPTR & ~FLASH_OPTR_RDP) | rdp;
		return WriteOptionBytes(optionBytes);
	}


	virtual ResultStatus DisableReadProtection() override {
		return SetReadProtectionLevel(FlashProtectionLevel::Level0);
	}


	virtual ResultStatus UnlockOptionBytes() override {
		if (!IsOptionBytesLocked()) {
			return ResultStatus::ok;
		}

		flashHandle->OPTKEYR = OptionKey1;
		flashHandle->OPTKEYR = OptionKey2;

		return IsOptionBytesLocked() ? ResultStatus::accessError : ResultStatus::ok;
	}


	virtual ResultStatus LockOptionBytes() override {
		flashHandle->CR |= FLASH_CR_OPTLOCK;
		return ResultStatus::ok;
	}


	virtual bool IsOptionBytesLocked() const override {
		return (flashHandle->CR & FLASH_CR_OPTLOCK) != 0;
	}


	// Forces the option reload, which resets the MCU. Not part of FLASHAdapter: the application calls it when a new
	// read-protection level must take effect now. Refused while FLASH_CR is locked, because the write would be dropped.
	ResultStatus LaunchOptionBytes() {
		if (IsLocked()) {
			return ResultStatus::accessError;
		}

		flashHandle->CR |= FLASH_CR_OBL_LAUNCH;
		return ResultStatus::ok;
	}


protected:
	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		// The flash has no clock gate, so the only thing to check is the region the bootloader gave us
		if (parameters.endAddress <= parameters.startAddress || parameters.startAddress < FLASH_BASE) {
			return ResultStatus::invalidParameter;
		}

		return AfterInitialization();
	}


private:
	// The region is compared in 64 bits so that address + size cannot wrap.
	bool IsFlashArea(uint64 address, uint64 size) const {
		return address >= parameters.startAddress && address + size <= parameters.endAddress;
	}


	bool IsLocked() const {
		return (flashHandle->CR & FLASH_CR_LOCK) != 0;
	}


	bool IsDualBank() const {
#if defined(FLASH_CR_BKER)
		return (flashHandle->OPTR & FLASH_OPTR_DBANK) != 0;
#else
		return false;
#endif
	}


	// Pages per bank on a dual-bank part. The banks split the device flash evenly, so the count is the total page count
	// halved, and the total comes from the factory flash-size register (in KB, 2 KB pages: KB / 2 pages, KB / 4 per bank).
	// The size is read rather than assumed because G4 variants differ in flash size.
	uint32 BankPageCount() const {
		uint32 flashKb = static_cast<uint32>(*reinterpret_cast<volatile uint32 *>(FLASHSIZE_BASE)) & 0x0000FFFFUL;

		return flashKb / 4;
	}


	bool WaitNotBusy(uint32 timeoutMs) const {
		uint64 startMs = System::GetMs();

		while ((flashHandle->SR & BusyMask) != 0) {
			if (System::GetMs() - startMs > timeoutMs) {
				return false;
			}
		}

		return true;
	}


	// The caches must be off while the flash changes. Returns the enable bits that were set, to be restored later.
	uint32 SuspendCaches() {
		uint32 acr = flashHandle->ACR;
		flashHandle->ACR = acr & ~CacheEnableMask;

		return acr & CacheEnableMask;
	}


	// Resets the caches so no stale line of the old contents survives, then restores the previous enable state.
	void ResetAndRestoreCaches(uint32 cachesEnabled) {
		flashHandle->ACR |= CacheResetMask;
		flashHandle->ACR &= ~CacheResetMask;
		flashHandle->ACR |= cachesEnabled;
	}


	// WriteData() guarantees that size is a multiple of the granule, so every double word is full and programmed once.
	ResultStatus ProgramBuffer(uint32 *address, const uint8 *source, size_t size) {
		uint32 *target = address;

		for (size_t offset = 0; offset < size; offset += WriteGranule) {
			uint32 low = 0;
			uint32 high = 0;
			std::memcpy(&low, source + offset, sizeof(uint32));
			std::memcpy(&high, source + offset + sizeof(uint32), sizeof(uint32));

			ResultStatus status = ProgramDoubleWord(target, low, high);
			if (status != ResultStatus::ok) {
				return status;
			}

			target += WriteGranule / sizeof(uint32);
		}

		return ResultStatus::ok;
	}


	ResultStatus ProgramDoubleWord(uint32 *address, uint32 low, uint32 high) {
		ClearStatusFlags();
		flashHandle->CR |= FLASH_CR_PG;

		// The hardware takes the two words in order, low first. The second store starts the operation.
		volatile uint32 *target = address;
		target[0] = low;
		target[1] = high;

		bool done = WaitNotBusy(ProgramTimeoutMs);
		flashHandle->CR &= ~FLASH_CR_PG;

		if (!done) {
			return ResultStatus::timeout;
		}

		return GetStatus();
	}


	ResultStatus ErasePage(uint32 pageIndex) {
		uint32 bankBits = 0;
		uint32 pageInBank = pageIndex;

#if defined(FLASH_CR_BKER)
		if (IsDualBank()) {
			uint32 bankPages = BankPageCount();

			if (pageIndex >= bankPages) {
				bankBits = BankSelectMask;
				pageInBank = pageIndex - bankPages;
			}
		}
#endif

		if (!WaitNotBusy(PageEraseTimeoutMs)) {
			return ResultStatus::timeout;
		}

		ClearStatusFlags();
		uint32 cachesEnabled = SuspendCaches();

		uint32 cr = flashHandle->CR;
		cr &= ~(PageNumberMask | BankSelectMask);
		cr |= (pageInBank << FLASH_CR_PNB_Pos) | bankBits | FLASH_CR_PER;
		flashHandle->CR = cr;

		ResultStatus status = StartAndWait(PageEraseTimeoutMs);
		flashHandle->CR &= ~FLASH_CR_PER;

		ResetAndRestoreCaches(cachesEnabled);

		return status;
	}


	ResultStatus EraseBank(uint32 massEraseBit) {
		ClearStatusFlags();
		flashHandle->CR |= massEraseBit;

		ResultStatus status = StartAndWait(MassEraseTimeoutMs);
		flashHandle->CR &= ~massEraseBit;

		return status;
	}


	ResultStatus StartAndWait(uint32 timeoutMs) {
		flashHandle->CR |= FLASH_CR_STRT;

		if (!WaitNotBusy(timeoutMs)) {
			return ResultStatus::timeout;
		}

		return GetStatus();
	}
};
