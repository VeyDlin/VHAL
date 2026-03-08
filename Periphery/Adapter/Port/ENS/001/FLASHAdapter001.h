#pragma once
#include <Adapter/FLASHAdapter.h>


using AFLASH = class FLASHAdapter001;

class FLASHAdapter001 : public FLASHAdapter<CMSDK_MTPREG_TypeDef> {
public:
	static inline constexpr uint32 MtpBase = CMSDK_MTP_BASE;      // 0x10000000
	static inline constexpr uint32 MtpSize = 0x8000;              // 32 KB
	static inline constexpr uint32 SectorSize = 1024;             // 1024 bytes per sector
	static inline constexpr uint32 SectorCount = 8;               // 8 sectors (sector 7 reserved for bootloader)
	static inline constexpr uint32 UnlockKey = 0x5A5A5A5A;

	uint32 inputBusClockHz = 0;

	FLASHAdapter001() = default;
	FLASHAdapter001(CMSDK_MTPREG_TypeDef *mtp, uint32 busClockHz) : FLASHAdapter(mtp), inputBusClockHz(busClockHz) {}


	ResultStatus Unlock(uint32 key1, uint32 key2) override {
		flashHandle->MTP_CLR = 0xFFFFFFFF;
		flashHandle->MTP_CR = 0x00000002;
		flashHandle->MTP_ACLR = 0x00000000;   // Unlock sectors 0-6 (sector 7 = bootloader)
		flashHandle->MTP_KEYR = key1;
		return ResultStatus::ok;
	}

	ResultStatus Lock() override {
		flashHandle->MTP_CR = GetCrClockValue();
		return ResultStatus::ok;
	}

	Result<uint8> Read(uint8 *address) override {
		return *address;
	}

	ResultStatus WriteData(uint32 *address, const void *data, size_t size) override {
		auto status = Unlock(UnlockKey, 0);
		if (status != ResultStatus::ok) {
			return status;
		}

		auto *src = static_cast<const uint8 *>(data);
		auto addr = reinterpret_cast<uint32>(address);

		// MTP writes 32 bits at a time
		size_t offset = 0;
		while (offset < size) {
			uint32 word = 0;
			size_t remaining = size - offset;
			size_t chunk = (remaining < 4) ? remaining : 4;

			for (size_t i = 0; i < chunk; i++) {
				word |= static_cast<uint32>(src[offset + i]) << (i * 8);
			}
			// Pad with 0xFF if partial word
			for (size_t i = chunk; i < 4; i++) {
				word |= 0xFFu << (i * 8);
			}

			*reinterpret_cast<volatile uint32 *>(addr + offset) = word;
			WaitBusy();
			offset += 4;
		}

		Lock();
		return ResultStatus::ok;
	}

	ResultStatus Write(uint16 *address, uint16 data) override {
		auto status = Unlock(UnlockKey, 0);
		if (status != ResultStatus::ok) {
			return status;
		}

		// MTP requires 32-bit writes, read-modify-write the aligned word
		auto addr = reinterpret_cast<uint32>(address);
		auto alignedAddr = addr & ~0x3u;
		uint32 word = *reinterpret_cast<volatile uint32 *>(alignedAddr);

		if (addr & 0x2) {
			word = (word & 0x0000FFFF) | (static_cast<uint32>(data) << 16);
		} else {
			word = (word & 0xFFFF0000) | data;
		}

		*reinterpret_cast<volatile uint32 *>(alignedAddr) = word;
		WaitBusy();

		Lock();
		return ResultStatus::ok;
	}

	ResultStatus PageErase(uint8 *address) override {
		// MTP uses sector erase, map page to sector
		auto addr = reinterpret_cast<uint32>(address);
		if (addr < MtpBase || addr >= MtpBase + MtpSize) {
			return ResultStatus::error;
		}
		uint32 sector = (addr - MtpBase) / SectorSize;
		return SectorErase(sector);
	}

	ResultStatus SectorErase(uint32 sectorNumber) override {
		if (sectorNumber >= SectorCount) {
			return ResultStatus::error;
		}

		auto status = Unlock(UnlockKey, 0);
		if (status != ResultStatus::ok) {
			return status;
		}

		// Erase sector by writing 0xFFFFFFFF to all words in the sector
		uint32 base = MtpBase + sectorNumber * SectorSize;
		for (uint32 i = 0; i < SectorSize; i += 4) {
			*reinterpret_cast<volatile uint32 *>(base + i) = 0xFFFFFFFF;
			WaitBusy();
		}

		Lock();
		return ResultStatus::ok;
	}

	ResultStatus MassErase() override {
		for (uint32 i = 0; i < SectorCount; i++) {
			auto status = SectorErase(i);
			if (status != ResultStatus::ok) {
				return status;
			}
		}
		return ResultStatus::ok;
	}

	ResultStatus GetStatus() override {
		uint32 sr = flashHandle->MTP_SR;
		// Bit 1: busy
		if (sr & 0x02) {
			return ResultStatus::busy;
		}
		return ResultStatus::ok;
	}

	ResultStatus ClearStatusFlags() override {
		flashHandle->MTP_CLR = 0xFFFFFFFF;
		return ResultStatus::ok;
	}

	// Option bytes - not supported on MTP
	Result<uint32> ReadOptionBytes() override {
		return ResultStatus::notSupported;
	}

	ResultStatus WriteOptionBytes(uint32 optionBytes) override {
		return ResultStatus::notSupported;
	}

	// Read protection
	bool IsReadProtected() const override {
		return false;
	}

	uint8 GetReadProtectionLevel() const override {
		return 0;
	}

	FlashProtectionLevel GetProtectionLevel() const override {
		return FlashProtectionLevel::Level0;
	}

	ResultStatus SetReadProtectionLevel(FlashProtectionLevel level) override {
		return ResultStatus::notSupported;
	}

	ResultStatus DisableReadProtection() override {
		return ResultStatus::notSupported;
	}

	ResultStatus UnlockOptionBytes() override {
		return ResultStatus::notSupported;
	}

	ResultStatus LockOptionBytes() override {
		return ResultStatus::notSupported;
	}

	bool IsOptionBytesLocked() const override {
		return true;
	}


protected:
	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		flashHandle->MTP_CR = GetCrClockValue();

		return AfterInitialization();
	}

private:
	static constexpr uint32 MtpTimeout = 1000; // ms

	bool WaitBusy() {
		uint32 tickStart = System::GetTick();
		while (flashHandle->MTP_SR & 0x02) {
			if ((System::GetTick() - tickStart) > MtpTimeout) {
				return false;
			}
		}
		return true;
	}

	uint32 GetCrClockValue() {
		// MTP_CR clock config: bits <1:0>
		// 0x01 = 16 MHz, 0x03 = 32 MHz
		if (inputBusClockHz >= 32000000) {
			return 0x03;
		}
		return 0x01;
	}
};
