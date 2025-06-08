#pragma once
#include "../IBootloader.h"


// Abstract bootloader class for working with Flash memory
// PURPOSE: base class for creating bootloaders on various platforms with Flash memory
// PROVIDES:
// - Common logic for Flash operations (read, write, firmware verification)
// - Magic number and firmware header verification
// - Basic implementation of memory read/write commands
// - RDP (Read Protection) support through flashAdapter
// INHERITOR MUST IMPLEMENT:
// - OnCheckBootloaderRequest() - bootloader entry logic
// - OnStartApplication() - platform-specific application start
// - OnEraseIteration() - iterative memory erase (sectors/pages)
// TEMPLATE PARAMETERS:
// - RxBufferSize: receive buffer size (default 1024)
// - AccumBufferSize: accumulation buffer size for encryption (default 512)
// - PacketDataMaxSize: TODO
template<size_t RxBufferSize = 1024, size_t AccumBufferSize = 512, size_t PacketDataMaxSize = 240>
class FLASHBootloader : public IBootloader<RxBufferSize, AccumBufferSize, PacketDataMaxSize> {
protected:
    using FirmwareHeader = typename IBootloader<RxBufferSize, AccumBufferSize>::FirmwareHeader;
    
    AFLASH& flashAdapter;
    
    const uint32 memoryStartAddress;
    const uint32 memorySize;
    

public:
    FLASHBootloader(
    	ICommunication& comm,
    	AFLASH& flash,
    	uint32 startAddr,
    	uint32 size
    ) :	IBootloader<RxBufferSize, AccumBufferSize>(comm),
    	flashAdapter(flash),
    	memoryStartAddress(startAddr),
    	memorySize(size)
    { }
    

    virtual ~FLASHBootloader() = default;


protected:
    // IBootloader hooks implementation
    virtual Status::statusType OnCheckAccessLevel() override {
        auto protectionLevel = flashAdapter.GetProtectionLevel();
        
        switch (protectionLevel) {
            case AFLASH::FlashProtectionLevel::Level0:
                // No protection - full access allowed
                return Status::ok;
                
            case AFLASH::FlashProtectionLevel::Level1:
                // Limited protection - bootloader can work but with restrictions
                // External debugger access is blocked but bootloader operations should work
                return Status::ok;
                
            case AFLASH::FlashProtectionLevel::Level2:
                // Maximum protection - bootloader cannot modify Flash memory
                // This is permanent protection that cannot be downgraded
                return Status::accessError;
                
            case AFLASH::FlashProtectionLevel::Unknown:
            default:
                // Unknown protection level - assume restricted access
                return Status::accessError;
        }
    }
    


    virtual Status::statusType OnInitialize() override {
        return flashAdapter.SetParameters({
            .startAddress = memoryStartAddress,
            .endAddress = memoryStartAddress + memorySize
        });
    }
    


    // Check if bootloader mode entry is needed
    // USED: at system start to decide whether to run application or enter bootloader
    // INHERITOR MUST:
    // - Check bootloader entry conditions (for example):
    //   * Button state: if (GPIO_ReadPin(BOOT_BUTTON) == LOW) return true;
    //   * Flag in SRAM/RTC: if (RTC->BKP0R == BOOTLOADER_FLAG) return true;
    //   * No valid firmware: if (!OnCheckFirmware()) return true;
    //   * Command from external device
    // - Return true if bootloader entry is needed
    // - Return false if application should be started
    // IMPORTANT: this method is called BEFORE initialization, so can only use basic checks
    virtual bool OnCheckBootloaderRequest() const override = 0;
    


    // Start application
    // USED: when decided not to enter bootloader but to start main application
    // INHERITOR MUST:
    // - Deinitialize bootloader peripherals (UART, SPI, timers, etc.)
    // - Reset interrupt vector table: SCB->VTOR = app_address;
    // - Get entry point from firmware (usually at app_address + 4)
    // - Set up stack: __set_MSP(*(uint32_t*)app_address);
    // - Jump to application through function pointer
    // EXAMPLE for STM32:
    //   uint32_t app_address = GetMemoryStartAddress() + sizeof(FirmwareHeader);
    //   uint32_t stack_pointer = *((uint32_t*)app_address);
    //   uint32_t reset_handler = *((uint32_t*)(app_address + 4));
    //   __set_MSP(stack_pointer);
    //   ((void(*)())reset_handler)();
    // IMPORTANT: after calling this method, control MUST NOT return to bootloader
    virtual bool OnStartApplication() override = 0;
    

protected:
    virtual uint32 GetMemoryStartAddress() const override {
        return memoryStartAddress;
    }
    


    virtual uint32 GetMemorySize() const override {
        return memorySize;
    }
    


    virtual bool IsMemoryProtected() const override {
        return flashAdapter.IsReadProtected();
    }
    


    virtual std::optional<uint8> GetProtectionLevel() const override {
        auto level = flashAdapter.GetProtectionLevel();
        switch (level) {
            case AFLASH::FlashProtectionLevel::Level0: return 0;
            case AFLASH::FlashProtectionLevel::Level1: return 1;
            case AFLASH::FlashProtectionLevel::Level2: return 2;
            default: return std::nullopt;
        }
    }
    


    // Iterative erase - abstract method
    // USED: during Erase command to erase memory in parts without blocking the system
    // INHERITOR MUST:
    // - Implement platform-specific erasing (sectors/pages/blocks)
    // - Erase ONE memory quantum per call (sector for STM32F4, page for STM32G0/G4)
    // - Update bytesErased with the actual number of bytes erased
    // - Return Status::ok if erase successful
    // - Return Status::inProgress if more iterations needed
    // - Return Status::error on error
    // PARAMETERS:
    // - address: current address to erase (may not be aligned to sector boundary)
    // - endAddress: end address of erase range
    // - iteration: current iteration number (0, 1, 2, ...)
    // - bytesErased: OUT parameter - how many bytes were erased in this iteration
    // EXAMPLE for STM32F4 (sector erase):
    //   uint32 sector = GetSectorFromAddress(address);
    //   uint32 sectorStart = GetSectorStartAddress(sector);
    //   uint32 sectorSize = GetSectorSize(sector);
    //   flashAdapter.EraseSector(sector);
    //   bytesErased = sectorSize;
    //   if (sectorStart + sectorSize >= endAddress) return Status::ok;
    //   return Status::inProgress;
    // IMPORTANT: method must be fast (<100ms) to not block BLE/UART
    virtual Status::statusType OnEraseIteration(uint32 address, uint32 endAddress, uint32 iteration, uint32& bytesErased) override = 0;
    


    virtual Status::statusType OnWriteMemory(uint32 address, std::span<const uint8> data) override {
        auto status = flashAdapter.Unlock();
        if (status != Status::ok) {
            return status;
        }
        
        status = flashAdapter.WriteData(
            reinterpret_cast<uint32*>(address),
            data.data(),
            data.size()
        );
        
        flashAdapter.Lock();
        return status;
    }
    


    virtual Status::statusType OnReadMemory(uint32 address, std::span<uint8> data) override {
        for (size_t i = 0; i < data.size(); i++) {
            auto result = flashAdapter.Read(
                reinterpret_cast<uint8*>(address + i)
            );
            if (result.type != Status::ok) {
                return result.type;
            }
            data[i] = result.data;
        }
        
        return Status::ok;
    }
    
    
    
    virtual Status::statusType OnUnlockDevice(std::span<const uint8> password) override {
        // Password verification must be implemented in the specific application
        // Here's an example of simple verification
        const uint8 expectedPassword[] = "UNLOCK_RDP_KEY!";  // 16 bytes
        
        if (password.size() < sizeof(expectedPassword) - 1) {
            return Status::error;
        }
        
        bool passwordCorrect = true;
        for (size_t i = 0; i < sizeof(expectedPassword) - 1; i++) {
            if (password[i] != expectedPassword[i]) {
                passwordCorrect = false;
                break;
            }
        }
        
        if (!passwordCorrect) {
            return Status::accessError;
        }
        
        return flashAdapter.DisableReadProtection();
    }
};
