#include "AES.h"
#include <cstring>
#include <algorithm>
#include <ranges>

// Explicit instantiation for common key sizes
template class AES<AESKeySize::AES128>;
template class AES<AESKeySize::AES192>;
template class AES<AESKeySize::AES256>;

template<AESKeySize KeySize>
void AES<KeySize>::KeyExpansion(const Key& key) noexcept {
    constexpr size_t Nk = KEY_SIZE / 4;
    constexpr size_t Nr = ROUNDS;
    
    uint8 temp[4];
    
    // Copy key to round key
    std::copy(key.begin(), key.end(), roundKey.begin());
    
    // All other round keys are found from the previous round keys
    for (size_t i = Nk; i < 4 * (Nr + 1); ++i) {
        size_t k = (i - 1) * 4;
        temp[0] = roundKey[k + 0];
        temp[1] = roundKey[k + 1];
        temp[2] = roundKey[k + 2];
        temp[3] = roundKey[k + 3];

        if (i % Nk == 0) {
            // RotWord
            uint8 u8tmp = temp[0];
            temp[0] = temp[1];
            temp[1] = temp[2];
            temp[2] = temp[3];
            temp[3] = u8tmp;

            // SubWord
            temp[0] = sbox[temp[0]];
            temp[1] = sbox[temp[1]];
            temp[2] = sbox[temp[2]];
            temp[3] = sbox[temp[3]];

            temp[0] ^= Rcon[i/Nk];
        }
        
        if constexpr (KeySize == AESKeySize::AES256) {
            if (i % Nk == 4) {
                // SubWord
                temp[0] = sbox[temp[0]];
                temp[1] = sbox[temp[1]];
                temp[2] = sbox[temp[2]];
                temp[3] = sbox[temp[3]];
            }
        }

        size_t j = i * 4; 
        size_t l = (i - Nk) * 4;
        roundKey[j + 0] = roundKey[l + 0] ^ temp[0];
        roundKey[j + 1] = roundKey[l + 1] ^ temp[1];
        roundKey[j + 2] = roundKey[l + 2] ^ temp[2];
        roundKey[j + 3] = roundKey[l + 3] ^ temp[3];
    }
}

template<AESKeySize KeySize>
void AES<KeySize>::SubBytes(std::array<std::array<uint8, 4>, 4>& state) const noexcept {
    for (auto& row : state) {
        for (auto& byte : row) {
            byte = sbox[byte];
        }
    }
}

template<AESKeySize KeySize>
void AES<KeySize>::InvSubBytes(std::array<std::array<uint8, 4>, 4>& state) const noexcept {
    for (auto& row : state) {
        for (auto& byte : row) {
            byte = rsbox[byte];
        }
    }
}

template<AESKeySize KeySize>
void AES<KeySize>::ShiftRows(std::array<std::array<uint8, 4>, 4>& state) const noexcept {
    uint8 temp;

    // Rotate first row 1 column to left
    temp = state[1][0];
    state[1][0] = state[1][1];
    state[1][1] = state[1][2];
    state[1][2] = state[1][3];
    state[1][3] = temp;

    // Rotate second row 2 columns to left
    temp = state[2][0];
    state[2][0] = state[2][2];
    state[2][2] = temp;

    temp = state[2][1];
    state[2][1] = state[2][3];
    state[2][3] = temp;

    // Rotate third row 3 columns to left
    temp = state[3][0];
    state[3][0] = state[3][3];
    state[3][3] = state[3][2];
    state[3][2] = state[3][1];
    state[3][1] = temp;
}

template<AESKeySize KeySize>
void AES<KeySize>::InvShiftRows(std::array<std::array<uint8, 4>, 4>& state) const noexcept {
    uint8 temp;

    // Rotate first row 1 column to right
    temp = state[1][3];
    state[1][3] = state[1][2];
    state[1][2] = state[1][1];
    state[1][1] = state[1][0];
    state[1][0] = temp;

    // Rotate second row 2 columns to right
    temp = state[2][0];
    state[2][0] = state[2][2];
    state[2][2] = temp;

    temp = state[2][1];
    state[2][1] = state[2][3];
    state[2][3] = temp;

    // Rotate third row 3 columns to right
    temp = state[3][0];
    state[3][0] = state[3][1];
    state[3][1] = state[3][2];
    state[3][2] = state[3][3];
    state[3][3] = temp;
}

template<AESKeySize KeySize>
void AES<KeySize>::MixColumns(std::array<std::array<uint8, 4>, 4>& state) const noexcept {
    uint8 Tmp, Tm, t;
    for (uint8 i = 0; i < 4; ++i) {
        t   = state[0][i];
        Tmp = state[0][i] ^ state[1][i] ^ state[2][i] ^ state[3][i];
        Tm  = state[0][i] ^ state[1][i]; Tm = xtime(Tm); state[0][i] ^= Tm ^ Tmp;
        Tm  = state[1][i] ^ state[2][i]; Tm = xtime(Tm); state[1][i] ^= Tm ^ Tmp;
        Tm  = state[2][i] ^ state[3][i]; Tm = xtime(Tm); state[2][i] ^= Tm ^ Tmp;
        Tm  = state[3][i] ^ t;           Tm = xtime(Tm); state[3][i] ^= Tm ^ Tmp;
    }
}

template<AESKeySize KeySize>
void AES<KeySize>::InvMixColumns(std::array<std::array<uint8, 4>, 4>& state) const noexcept {
    for (uint8 i = 0; i < 4; ++i) {
        uint8 a = state[0][i];
        uint8 b = state[1][i];
        uint8 c = state[2][i];
        uint8 d = state[3][i];

        state[0][i] = multiply(a, 0x0e) ^ multiply(b, 0x0b) ^ multiply(c, 0x0d) ^ multiply(d, 0x09);
        state[1][i] = multiply(a, 0x09) ^ multiply(b, 0x0e) ^ multiply(c, 0x0b) ^ multiply(d, 0x0d);
        state[2][i] = multiply(a, 0x0d) ^ multiply(b, 0x09) ^ multiply(c, 0x0e) ^ multiply(d, 0x0b);
        state[3][i] = multiply(a, 0x0b) ^ multiply(b, 0x0d) ^ multiply(c, 0x09) ^ multiply(d, 0x0e);
    }
}

template<AESKeySize KeySize>
void AES<KeySize>::AddRoundKey(std::array<std::array<uint8, 4>, 4>& state, uint8 round) const noexcept {
    for (uint8 i = 0; i < 4; ++i) {
        for (uint8 j = 0; j < 4; ++j) {
            state[j][i] ^= roundKey[(round * 4 * 4) + (i * 4) + j];
        }
    }
}

template<AESKeySize KeySize>
void AES<KeySize>::Cipher(const uint8* input, uint8* output) const noexcept {
    std::array<std::array<uint8, 4>, 4> state;

    // Copy input to state array
    for (uint8 i = 0; i < 4; ++i) {
        for (uint8 j = 0; j < 4; ++j) {
            state[j][i] = input[i * 4 + j];
        }
    }

    // Add the First round key to the state before starting the rounds
    AddRoundKey(state, 0);

    // There will be Nr rounds
    for (uint8 round = 1; round < ROUNDS; ++round) {
        SubBytes(state);
        ShiftRows(state);
        MixColumns(state);
        AddRoundKey(state, round);
    }

    // The last round is given below
    SubBytes(state);
    ShiftRows(state);
    AddRoundKey(state, ROUNDS);

    // Copy state array to output
    for (uint8 i = 0; i < 4; ++i) {
        for (uint8 j = 0; j < 4; ++j) {
            output[i * 4 + j] = state[j][i];
        }
    }
}

template<AESKeySize KeySize>
void AES<KeySize>::InvCipher(const uint8* input, uint8* output) const noexcept {
    std::array<std::array<uint8, 4>, 4> state;

    // Copy input to state array
    for (uint8 i = 0; i < 4; ++i) {
        for (uint8 j = 0; j < 4; ++j) {
            state[j][i] = input[i * 4 + j];
        }
    }

    // Add the last round key to the state before starting the rounds
    AddRoundKey(state, ROUNDS);

    // There will be Nr rounds
    for (int round = ROUNDS - 1; round > 0; --round) {
        InvShiftRows(state);
        InvSubBytes(state);
        AddRoundKey(state, round);
        InvMixColumns(state);
    }

    // The last round is given below
    InvShiftRows(state);
    InvSubBytes(state);
    AddRoundKey(state, 0);

    // Copy state array to output
    for (uint8 i = 0; i < 4; ++i) {
        for (uint8 j = 0; j < 4; ++j) {
            output[i * 4 + j] = state[j][i];
        }
    }
}

template<AESKeySize KeySize>
void AES<KeySize>::EncryptCBC(std::span<uint8> data) noexcept {
    if (!iv.has_value() || data.size() % BLOCK_SIZE != 0) {
        return; // CBC requires IV and data must be padded
    }

    Block ivCopy = iv.value();
    
    for (size_t i = 0; i < data.size(); i += BLOCK_SIZE) {
        // XOR with IV or previous ciphertext
        for (size_t j = 0; j < BLOCK_SIZE; ++j) {
            data[i + j] ^= ivCopy[j];
        }
        
        // Encrypt block
        Cipher(&data[i], &data[i]);
        
        // Copy ciphertext for next block
        std::copy_n(&data[i], BLOCK_SIZE, ivCopy.begin());
    }
}

template<AESKeySize KeySize>
void AES<KeySize>::DecryptCBC(std::span<uint8> data) noexcept {
    if (!iv.has_value() || data.size() % BLOCK_SIZE != 0) {
        return; // CBC requires IV and data must be padded
    }

    Block ivCopy = iv.value();
    Block nextIv;
    
    for (size_t i = 0; i < data.size(); i += BLOCK_SIZE) {
        // Save ciphertext for next block
        std::copy_n(&data[i], BLOCK_SIZE, nextIv.begin());
        
        // Decrypt block
        InvCipher(&data[i], &data[i]);
        
        // XOR with IV or previous ciphertext
        for (size_t j = 0; j < BLOCK_SIZE; ++j) {
            data[i + j] ^= ivCopy[j];
        }
        
        ivCopy = nextIv;
    }
}

template<AESKeySize KeySize>
void AES<KeySize>::EncryptCTR(std::span<uint8> data) noexcept {
    if (!iv.has_value()) {
        return; // CTR requires IV
    }

    Block counter = iv.value();
    Block keystream;
    
    for (size_t i = 0; i < data.size(); i += BLOCK_SIZE) {
        // Encrypt counter to get keystream
        Cipher(counter.data(), keystream.data());
        
        // XOR data with keystream
        size_t blockSize = std::min(BLOCK_SIZE, data.size() - i);
        for (size_t j = 0; j < blockSize; ++j) {
            data[i + j] ^= keystream[j];
        }
        
        // Increment counter
        for (int j = BLOCK_SIZE - 1; j >= 0; --j) {
            if (++counter[j] != 0) break;
        }
    }
}