#pragma once
#include "FLASHBootloader.h"
#include <Utilities/Crypto/AES/AES.h>
#include <algorithm>
#include <array>
#include <span>


// Bootloader with AES encryption support (AES-128/192/256, ECB/CBC/CTR).
// Incoming image data is decrypted before it is written to flash.
template<AESKeySize KeySize, size_t RxBufferSize = 1024, size_t AccumBufferSize = 512, size_t PacketDataMaxSize = 240>
class AESFLASHBootloader : public FLASHBootloader<RxBufferSize, AccumBufferSize, PacketDataMaxSize> {
protected:
    using Base = FLASHBootloader<RxBufferSize, AccumBufferSize, PacketDataMaxSize>;

    AESMode aesMode;
    AES<KeySize> aes;
    typename AES<KeySize>::IV iv;           // Original Initialization Vector for CBC/CTR
    typename AES<KeySize>::IV currentIV;    // Current IV state for CBC chaining
    std::array<uint8, AccumBufferSize> decryptionBuffer;
    uint64 ctrStreamOffset = 0;             // CTR keystream position in bytes, continuous over the whole image
    bool keyValid = false;                  // False when the key argument had the wrong size
    
    static constexpr size_t keySize = static_cast<size_t>(KeySize);


public:
    template<typename KeyRange>
    AESFLASHBootloader(
        ICommunication& comm,
        AFLASH& flash,
        AESMode mode,
        const KeyRange& key,
        uint32 startAddr,
        uint32 size,
        const typename AES<KeySize>::IV& initVector = {}
    ) :
        Base(comm, flash, startAddr, size),
        aesMode(mode),
        aes(MakeKey(key)),
        iv(initVector),
        currentIV(initVector),
        keyValid(IsKeySizeValid(key))
    {
        using KeyView = decltype(std::span(key));
        static_assert(
            KeyView::extent == std::dynamic_extent || KeyView::extent == keySize,
            "Key size does not match the AES key size"
        );
    }



    virtual ~AESFLASHBootloader() {
        // Clear the IV state and the decrypted data buffer.
        // AES does not wipe its round keys.
        std::fill(iv.begin(), iv.end(), 0);
        std::fill(currentIV.begin(), currentIV.end(), 0);
        std::fill(decryptionBuffer.begin(), decryptionBuffer.end(), 0);
    }



    // Methods for IV/Nonce management
    void SetIV(const typename AES<KeySize>::IV& newIV) {
        iv = newIV;
        currentIV = newIV;  // Reset current IV for new stream
        ctrStreamOffset = 0;
        aes.SetIV(iv);
    }



    const typename AES<KeySize>::IV& GetIV() const {
        return iv;
    }



    AESMode GetMode() const {
        return aesMode;
    }



    static constexpr AESKeySize GetKeySize() {
        return KeySize;
    }



protected:
    virtual ResultStatus OnBeforeInitialize() override {
        if (!keyValid) {
            return ResultStatus::invalidParameter;
        }

        return Base::OnBeforeInitialize();
    }


    
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



    // Decrypts ciphertext into decryptionBuffer. The ciphertext is left intact,
    // so a retried Write can be decrypted again.
    virtual std::span<uint8> OnDecryptData(std::span<uint8> data) override {
        if (!keyValid || data.empty() || data.size() > decryptionBuffer.size()) {
            // Return empty span on error
            return std::span<uint8>();
        }

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
        
        return std::span<uint8>(decryptionBuffer.data(), outputSize);
    }



    // A new image stream starts from the original IV (CBC chain restarts, CTR offset returns to 0)
    virtual void OnStreamReset() override {
        Base::OnStreamReset();
        currentIV = iv;
        ctrStreamOffset = 0;
    }



    // Ciphertext must not be read back: the flash holds plaintext after decryption
    virtual bool IsReadBackAllowed() const override {
        return false;
    }



private:
    static typename AES<KeySize>::Key MakeKey(std::span<const uint8> keyData) {
        typename AES<KeySize>::Key key{};
        if (keyData.size() == keySize) {
            std::copy(keyData.begin(), keyData.end(), key.begin());
        }
        return key;
    }
        

        
    static bool IsKeySizeValid(std::span<const uint8> keyData) {
        return keyData.size() == keySize;
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



    size_t DecryptCBC(std::span<uint8> data) {
        size_t blocks = data.size() / AES<>::BLOCK_SIZE;
        size_t outputSize = blocks * AES<>::BLOCK_SIZE;
        
        if (outputSize == 0 || outputSize > decryptionBuffer.size()) {
            return 0;
        }

        // Copy data to buffer 
        std::copy_n(data.begin(), outputSize, decryptionBuffer.begin());
        
        // Use current IV (which may be updated from previous call)
        aes.SetIV(currentIV);
        aes.DecryptCBC(std::span<uint8>(decryptionBuffer.data(), outputSize));
        
        // Keep the last ciphertext block of the processed part as the chain value for the next call
        std::copy_n(data.data() + (outputSize - AES<>::BLOCK_SIZE), AES<>::BLOCK_SIZE, currentIV.begin());
        
        return outputSize;
    }



    // CTR keystream block i is AES-ECB(IV + i), where + is a 128-bit big-endian addition
    typename AES<KeySize>::Block MakeCounterBlock(uint64 blockIndex) const {
        typename AES<KeySize>::Block counter{};
        std::copy(iv.begin(), iv.end(), counter.begin());

        // Add blockIndex to the big-endian counter, propagating the carry from the last byte
        uint16 carry = 0;
        for (size_t i = AES<KeySize>::BLOCK_SIZE; i > 0; i--) {
            uint16 sum = static_cast<uint16>(counter[i - 1]) + static_cast<uint16>(blockIndex & 0xFF) + carry;
            counter[i - 1] = static_cast<uint8>(sum & 0xFF);
            carry = static_cast<uint16>(sum >> 8);
            blockIndex >>= 8;
        }

        return counter;
    }



    size_t DecryptCTR(std::span<uint8> data) {
        // CTR is one continuous keystream over the whole image, so the offset carries over between Write calls
        std::copy_n(data.begin(), data.size(), decryptionBuffer.begin());

        size_t processed = 0;
        while (processed < data.size()) {
            uint64 streamPosition = ctrStreamOffset + processed;
            uint64 blockIndex = streamPosition / AES<KeySize>::BLOCK_SIZE;
            size_t blockOffset = static_cast<size_t>(streamPosition % AES<KeySize>::BLOCK_SIZE);

            typename AES<KeySize>::Block keystream = MakeCounterBlock(blockIndex);
            aes.EncryptECB(keystream);

            while (blockOffset < AES<KeySize>::BLOCK_SIZE && processed < data.size()) {
                decryptionBuffer[processed] ^= keystream[blockOffset];
                processed++;
                blockOffset++;
            }
        }

        ctrStreamOffset += data.size();
        return data.size();
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
