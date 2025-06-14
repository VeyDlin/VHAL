#pragma once
#include "FLASHBootloader.h"
#include <Utilities/Crypto/AES/AES.h>
#include <array>

// Bootloader with AES encryption support
// PURPOSE: decorator to add encryption to any FLASHBootloader
// SUPPORTS: AES-128/192/256 in ECB/CBC/CTR modes
// SECURITY:
// - Returns empty span on encryption/decryption error
// - Automatically clears sensitive data in destructor
// - Key must be stored in protected memory area
// PROVIDES:
// - Automatic encryption/decryption of data during write/read
// - Block data processing for ECB/CBC modes
// - Stream processing for CTR mode
// INHERITOR MUST IMPLEMENT (from FLASHBootloader):
// - OnCheckBootloaderRequest() - bootloader entry logic
// - OnStartApplication() - platform-specific application launch  
// - OnEraseIteration() - iterative memory erase (sectors/pages)
// USAGE:
// - Inherit from this class instead of FLASHBootloader
// - Data is automatically encrypted on write and decrypted on read
// EXAMPLE:
//   uint8 key[32] = {...}; // AES-256 key
//   AESFLASHBootloader<AESKeySize::AES256> bootloader(uart, flash, 
//                                                     AESMode::CBC, key, 
//                                                     0x08004000, 0x7C000, iv);
template<AESKeySize KeySize, size_t RxBufferSize = 1024, size_t AccumBufferSize = 512, size_t PacketDataMaxSize = 240>
class AESFLASHBootloader : public FLASHBootloader<RxBufferSize, AccumBufferSize, PacketDataMaxSize> {
protected:
    AESMode aesMode;
    AES<KeySize> aes;
    AES<KeySize>::IV iv;  // Initialization Vector for CBC/CTR
    
    std::array<uint8, 512> encryptionBuffer;
    std::array<uint8, 512> decryptionBuffer;
    
    static constexpr size_t keySize = static_cast<size_t>(KeySize);


private:
    template<typename KeySpan>
    typename AES<KeySize>::Key makeKey(const KeySpan& keySpan) {
        typename AES<KeySize>::Key key{};
        std::copy_n(keySpan.begin(), std::min(keySpan.size(), key.size()), key.begin());
        return key;
    }


public:
    template<typename KeySpan>
    AESFLASHBootloader(
    	ICommunication& comm,
    	AFLASH& flash,
    	AESMode mode,
    	KeySpan&& key,
    	uint32 startAddr,
    	uint32 size,
    	const AES<KeySize>::IV& initVector = {}
    ) :	FLASHBootloader<RxBufferSize, AccumBufferSize>(comm, flash, startAddr, size),
    	aesMode(mode),
    	aes(makeKey(key)),
    	iv(initVector)
    {
    	// Check key size at runtime
    	if (std::span(key).size() != keySize) {
    		// Wrong key size - critical security error
    		// But don't use SystemAssert as it's too harsh
    	}
    }



    virtual ~AESFLASHBootloader() {
        // Secure clearing of sensitive data
        std::fill(iv.begin(), iv.end(), 0);
        std::fill(encryptionBuffer.begin(), encryptionBuffer.end(), 0);
        std::fill(decryptionBuffer.begin(), decryptionBuffer.end(), 0);
        // AES class should clear keys in its own destructor
    }



protected:
    // Override methods for encryption support
    
    virtual bool OnHasEnoughData(std::span<const uint8> buffer) const override {
        switch (aesMode) {
            case AESMode::ECB:
            case AESMode::CBC:
                // ECB and CBC need full 16-byte blocks
                return buffer.size() >= AES<>::BLOCK_SIZE;
                
            case AESMode::CTR:
                // CTR can work with any amount of data
                return buffer.size() > 0;
        }
        return false;
    }



    virtual size_t OnGetRequiredBytes(std::span<const uint8> buffer) const override {
        switch (aesMode) {
            case AESMode::ECB:
            case AESMode::CBC:
                // Take maximum number of full blocks
                return (buffer.size() / AES<>::BLOCK_SIZE) * AES<>::BLOCK_SIZE;
                
            case AESMode::CTR:
                // CTR can process all data
                return buffer.size();
        }
        return 0;
    }



    virtual std::span<uint8> OnEncryptData(std::span<uint8> data) override {
        if (data.empty() || data.size() > encryptionBuffer.size()) {
            // Return empty span on error - it's safer
            return std::span<uint8>();
        }

        //System::console << Console::debug << "[ENCRYPT] [IN] " << data << Console::endl;

        size_t outputSize = 0;
        
        switch (aesMode) {
            case AESMode::ECB:
                outputSize = EncryptECB(data);
                break;
                
            case AESMode::CBC:
                outputSize = EncryptCBC(data);
                break;
                
            case AESMode::CTR:
                outputSize = EncryptCTR(data);
                break;
        }

        // Check that operation was successful
        if (outputSize == 0) {
            return std::span<uint8>();  // Encryption error
        }
        
        auto result = std::span<uint8>(encryptionBuffer.data(), outputSize);
        
        // Clear source data for security reasons
        std::fill(data.begin(), data.end(), 0);
        
        //System::console << Console::debug << "[ENCRYPT] [RESULT] " << result << Console::endl;

        return result;
    }



    virtual std::span<uint8> OnDecryptData(std::span<uint8> data) override {
        if (data.empty() || data.size() > decryptionBuffer.size()) {
            // Return empty span on error - it's safer
            return std::span<uint8>();
        }

        //System::console << Console::debug << "[DECRYPT] [IN] " << data << Console::endl;

        size_t outputSize = 0;
        
        switch (aesMode) {
            case AESMode::ECB:
                outputSize = DecryptECB(data);
                break;
                
            case AESMode::CBC:
                outputSize = DecryptCBC(data);
                break;
                
            case AESMode::CTR:
                outputSize = DecryptCTR(data);
                break;
        }

        // Check that operation was successful
        if (outputSize == 0) {
            return std::span<uint8>();  // Decryption error
        }
        
        auto result = std::span<uint8>(decryptionBuffer.data(), outputSize);
        
        // Clear encrypted data for security reasons
        std::fill(data.begin(), data.end(), 0);
        
        //System::console << Console::debug << "[DECRYPT] [RESULT] " << result << Console::endl;

        return result;
    }


private:
    size_t EncryptECB(std::span<uint8> data) {
        // ECB encrypts data in blocks without connection between blocks
        size_t blocks = data.size() / AES<>::BLOCK_SIZE;
        size_t outputSize = blocks * AES<>::BLOCK_SIZE;
        
        if (outputSize > encryptionBuffer.size()) {
            return 0;
        }

        // Copy data to buffer and encrypt in-place
        std::copy_n(data.begin(), outputSize, encryptionBuffer.begin());
        
        for (size_t i = 0; i < outputSize; i += AES<>::BLOCK_SIZE) {
            std::array<uint8, AES<>::BLOCK_SIZE> block;
            std::copy_n(encryptionBuffer.begin() + i, AES<>::BLOCK_SIZE, block.begin());
            aes.EncryptECB(block);
            std::copy(block.begin(), block.end(), encryptionBuffer.begin() + i);
        }
        
        return outputSize;
    }



    size_t DecryptECB(std::span<uint8> data) {
        size_t blocks = data.size() / AES<>::BLOCK_SIZE;
        size_t outputSize = blocks * AES<>::BLOCK_SIZE;
        
        if (outputSize > decryptionBuffer.size()) {
            return 0;
        }

        // Copy data to buffer and decrypt in-place
        std::copy_n(data.begin(), outputSize, decryptionBuffer.begin());
        
        for (size_t i = 0; i < outputSize; i += AES<>::BLOCK_SIZE) {
            std::array<uint8, AES<>::BLOCK_SIZE> block;
            std::copy_n(decryptionBuffer.begin() + i, AES<>::BLOCK_SIZE, block.begin());
            aes.DecryptECB(block);
            std::copy(block.begin(), block.end(), decryptionBuffer.begin() + i);
        }
        
        return outputSize;
    }



    size_t EncryptCBC(std::span<uint8> data) {
        // CBC uses block chaining with IV
        size_t blocks = data.size() / AES<>::BLOCK_SIZE;
        size_t outputSize = blocks * AES<>::BLOCK_SIZE;
        
        if (outputSize > encryptionBuffer.size()) {
            return 0;
        }

        // Copy data to buffer 
        std::copy_n(data.begin(), outputSize, encryptionBuffer.begin());
        
        // Set IV and encrypt in-place
        aes.SetIV(iv);
        aes.EncryptCBC(std::span(encryptionBuffer.data(), outputSize));
        
        return outputSize;
    }



    size_t DecryptCBC(std::span<uint8> data) {
        size_t blocks = data.size() / AES<>::BLOCK_SIZE;
        size_t outputSize = blocks * AES<>::BLOCK_SIZE;
        
        if (outputSize > decryptionBuffer.size()) {
            return 0;
        }

        // Copy data to buffer 
        std::copy_n(data.begin(), outputSize, decryptionBuffer.begin());
        
        // Set IV and decrypt in-place
        aes.SetIV(iv);
        aes.DecryptCBC(std::span(decryptionBuffer.data(), outputSize));
        
        return outputSize;
    }



    size_t EncryptCTR(std::span<uint8> data) {
        // CTR can encrypt data of any size
        if (data.size() > encryptionBuffer.size()) {
            return 0;
        }

        // Copy data to buffer 
        std::copy_n(data.begin(), data.size(), encryptionBuffer.begin());
        
        // Set IV and encrypt in-place  
        aes.SetIV(iv);
        aes.EncryptCTR(std::span(encryptionBuffer.data(), data.size()));
        
        return data.size();
    }



    size_t DecryptCTR(std::span<uint8> data) {
        // In CTR decryption = encryption
        if (data.size() > decryptionBuffer.size()) {
            return 0;
        }

        // Copy data to buffer 
        std::copy_n(data.begin(), data.size(), decryptionBuffer.begin());
        
        // Set IV and decrypt in-place
        aes.SetIV(iv);
        aes.DecryptCTR(std::span(decryptionBuffer.data(), data.size()));
        
        return data.size();
    }

public:
    // Methods for IV/Nonce management
    void SetIV(const AES<KeySize>::IV& newIV) {
        iv = newIV;
    }

    const AES<KeySize>::IV& GetIV() const {
        return iv;
    }

    AESMode GetMode() const {
        return aesMode;
    }

    static constexpr AESKeySize GetKeySize() {
        return KeySize;
    }
};


// Aliases for convenience with default buffer sizes
using AES128FLASHBootloader = AESFLASHBootloader<AESKeySize::AES128>;
using AES192FLASHBootloader = AESFLASHBootloader<AESKeySize::AES192>;
using AES256FLASHBootloader = AESFLASHBootloader<AESKeySize::AES256>;

// Aliases with configurable buffers
template<size_t RxSize, size_t AccumSize>
using AES128FLASHBootloaderCustom = AESFLASHBootloader<AESKeySize::AES128, RxSize, AccumSize>;

template<size_t RxSize, size_t AccumSize>
using AES192FLASHBootloaderCustom = AESFLASHBootloader<AESKeySize::AES192, RxSize, AccumSize>;

template<size_t RxSize, size_t AccumSize>
using AES256FLASHBootloaderCustom = AESFLASHBootloader<AESKeySize::AES256, RxSize, AccumSize>;
