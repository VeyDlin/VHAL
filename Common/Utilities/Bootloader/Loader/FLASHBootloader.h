#pragma once
#include "../IBootloader.h"


// Abstract bootloader class for working with Flash memory
template<size_t RxBufferSize = 1024, size_t AccumBufferSize = 512, size_t PacketDataMaxSize = 240>
class FLASHBootloader : public IBootloader<RxBufferSize, AccumBufferSize, PacketDataMaxSize> {
public:
    // Largest write alignment (in bytes) the flash write path supports. The 8-byte firmware header at the end of the
    // region is written as one piece, so the alignment can not be larger than the header.
    static constexpr uint32 maxWriteAlignment = 8;


protected:
    using Base = IBootloader<RxBufferSize, AccumBufferSize, PacketDataMaxSize>;
    using FirmwareHeader = typename Base::FirmwareHeader;
    static_assert(sizeof(FirmwareHeader) % maxWriteAlignment == 0, "the firmware header must be writable in whole units");

    AFLASH& flashAdapter;
    
    const uint32 memoryStartAddress;
    const uint32 memorySize;
    const uint32 writeAlignment;
    

public:
    FLASHBootloader(
        ICommunication& comm,
        AFLASH& flash,
        uint32 startAddr,
        uint32 size,
        uint32 alignment = 4
    ) : Base(comm),
        flashAdapter(flash),
        memoryStartAddress(startAddr),
        memorySize(size),
        writeAlignment(alignment == 0 ? 1 : alignment)
    {
    }
    

    virtual ~FLASHBootloader() = default;


protected:
    virtual ResultStatus OnBeforeInitialize() override {
        // A power of two up to the header size, with an aligned region: then the header address at the end of the
        // region is aligned too and every write unit fits into the tail buffer.
        bool isPowerOfTwo = (writeAlignment & (writeAlignment - 1)) == 0;
        if (
            writeAlignment > maxWriteAlignment ||
            !isPowerOfTwo ||
            memoryStartAddress % writeAlignment != 0 ||
            memorySize % writeAlignment != 0
        ) {
            return ResultStatus::invalidParameter;
        }

        return flashAdapter.SetParameters({
            .startAddress = memoryStartAddress,
            .endAddress = memoryStartAddress + memorySize
        });
    }
    


    virtual ResultStatus OnCheckAccessLevel() override {
        AFLASH::FlashProtectionLevel protectionLevel = flashAdapter.GetProtectionLevel();
        
        switch (protectionLevel) {
            case AFLASH::FlashProtectionLevel::Level0:
                // No protection - full access allowed
                return ResultStatus::ok;
                
            case AFLASH::FlashProtectionLevel::Level1:
                // Read-out protection: the level itself does not forbid flash changes. Erase/Write/Finalize are
                // still refused through IsMemoryProtected() unless allowUpdatesWhenReadProtected is set
                return ResultStatus::ok;
                
            case AFLASH::FlashProtectionLevel::Level2:
                // Policy: updates are refused at Level 2
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
    


    virtual uint32 GetWriteAlignment() const override {
        return writeAlignment;
    }



    virtual bool IsMemoryProtected() const override {
        return flashAdapter.IsReadProtected();
    }
    


    virtual std::optional<uint8> GetProtectionLevel() const override {
        AFLASH::FlashProtectionLevel level = flashAdapter.GetProtectionLevel();
        switch (level) {
            case AFLASH::FlashProtectionLevel::Level0: return 0;
            case AFLASH::FlashProtectionLevel::Level1: return 1;
            case AFLASH::FlashProtectionLevel::Level2: return 2;
            default: return std::nullopt;
        }
    }
    


    virtual ResultStatus OnEraseIteration(uint32 address, uint32 endAddress, uint32 iteration, uint32& bytesErased) override = 0;
    


    virtual ResultStatus OnWriteMemory(uint32 address, std::span<const uint8> data) override {
        if (data.empty()) {
            return ResultStatus::error;
        }

        // Writes must start on an alignment boundary
        if (address % writeAlignment != 0) {
            return ResultStatus::error;
        }

        ResultStatus status = flashAdapter.Unlock();
        if (status != ResultStatus::ok) {
            return status;
        }
        
        // Aligned prefix is written directly from the source
        const size_t alignedSize = data.size() - data.size() % writeAlignment;
        if (alignedSize > 0) {
            status = flashAdapter.WriteData(
                reinterpret_cast<uint32*>(address),
                data.data(),
                alignedSize
            );
        }
        
        // Unaligned tail goes through a padded buffer so the adapter never reads past 'data'
        const size_t tailSize = data.size() - alignedSize;
        if (status == ResultStatus::ok && tailSize > 0) {
            std::array<uint8, maxWriteAlignment> tailBuffer;
            tailBuffer.fill(0xFF);
            std::copy(data.begin() + alignedSize, data.end(), tailBuffer.begin());

            status = flashAdapter.WriteData(
                reinterpret_cast<uint32*>(static_cast<uint32>(address + alignedSize)),
                tailBuffer.data(),
                writeAlignment
            );
        }
        
        flashAdapter.Lock();
        
        return status;
    }
    


    virtual ResultStatus OnReadMemory(uint32 address, std::span<uint8> data) override {
        for (size_t i = 0; i < data.size(); i++) {
            Result<uint8> result = flashAdapter.Read(
                reinterpret_cast<uint8*>(address + i)
            );
            if (result.IsErr()) {
                return result.Error();
            }
            data[i] = result.Value();
        }

        return ResultStatus::ok;
    }
    
    
    
    // Checks a password against an expected secret and removes read-out protection on a match.
    // The comparison is constant-time: no early exit on the first mismatching byte.
    // On STM32 the RDP 1 -> 0 regression mass-erases the flash.
    ResultStatus UnlockWithPassword(std::span<const uint8> password, std::span<const uint8> expected) {
        if (expected.empty() || password.size() != expected.size()) {
            return ResultStatus::accessError;
        }
        
        uint8 difference = 0;
        for (size_t i = 0; i < expected.size(); i++) {
            difference |= static_cast<uint8>(password[i] ^ expected[i]);
        }
        
        if (difference != 0) {
            return ResultStatus::accessError;
        }
        
        return flashAdapter.DisableReadProtection();
    }
};
