#pragma once
#include "../IBootloader.h"


// Abstract bootloader class for working with Flash memory
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
    virtual ResultStatus OnBeforeInitialize() override {
        return flashAdapter.SetParameters({
            .startAddress = memoryStartAddress,
            .endAddress = memoryStartAddress + memorySize
        });
    }
    


    virtual ResultStatus OnCheckAccessLevel() override {
        auto protectionLevel = flashAdapter.GetProtectionLevel();
        
        switch (protectionLevel) {
            case AFLASH::FlashProtectionLevel::Level0:
                // No protection - full access allowed
                return ResultStatus::ok;
                
            case AFLASH::FlashProtectionLevel::Level1:
                // Limited protection - bootloader can work but with restrictions
                // External debugger access is blocked but bootloader operations should work
                return ResultStatus::ok;
                
            case AFLASH::FlashProtectionLevel::Level2:
                // Maximum protection - bootloader cannot modify Flash memory
                // This is permanent protection that cannot be downgraded
                return ResultStatus::accessError;
                
            case AFLASH::FlashProtectionLevel::Unknown:
            default:
                // Unknown protection level - assume restricted access
                return ResultStatus::accessError;
        }
    }
    


    virtual bool OnCheckBootloaderRequest() const override = 0;
    


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
    


    virtual ResultStatus OnEraseIteration(uint32 address, uint32 endAddress, uint32 iteration, uint32& bytesErased) override = 0;
    


    virtual ResultStatus OnWriteMemory(uint32 address, std::span<const uint8> data) override {
        auto status = flashAdapter.Unlock();
        if (status != ResultStatus::ok) {
            return status;
        }
        
        if (data.data() == nullptr) {
            return ResultStatus::error;
        }
        
        status = flashAdapter.WriteData(
            reinterpret_cast<uint32*>(address),
            data.data(),
            data.size()
        );
        
        flashAdapter.Lock();
        
        return status;
    }
    


    virtual ResultStatus OnReadMemory(uint32 address, std::span<uint8> data) override {
        for (size_t i = 0; i < data.size(); i++) {
            auto result = flashAdapter.Read(
                reinterpret_cast<uint8*>(address + i)
            );
            if (result.IsErr()) {
                return result.Error();
            }
            data[i] = result.Value();
        }

        return ResultStatus::ok;
    }
    
    
    
    virtual ResultStatus OnUnlockDevice(std::span<const uint8> password) override {
        // Password verification must be implemented in the specific application
        // Here's an example of simple verification
        const uint8 expectedPassword[] = "UNLOCK_RDP_KEY!";  // 16 bytes
        
        if (password.size() < sizeof(expectedPassword) - 1) {
            return ResultStatus::error;
        }
        
        bool passwordCorrect = true;
        for (size_t i = 0; i < sizeof(expectedPassword) - 1; i++) {
            if (password[i] != expectedPassword[i]) {
                passwordCorrect = false;
                break;
            }
        }
        
        if (!passwordCorrect) {
            return ResultStatus::accessError;
        }
        
        return flashAdapter.DisableReadProtection();
    }
};
