#pragma once
#include <BSP.h>
#include "ICommunication.h"
#include <Utilities/Buffer/RingBuffer.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <array>
#include <algorithm>
#include <optional>


// Abstract bootloader class with base protocol logic
// 
// ARCHITECTURE:
// IBootloader manages the entire process and protocol
// Inheritors implement only platform-specific operations
//
// IBootloader RESPONSIBILITIES:
// - Protocol command parsing and validation
// - State management (Idle, Processing, Error)
// - Automatic Busy response when occupied
// - Asynchronous operation management (partial erase)
// - Data buffering for Write commands
// - Encryption/decryption through virtual methods
//
// INHERITOR RESPONSIBILITIES:
// - Implement OnCheckBootloaderRequest() - bootloader entry condition
// - Implement OnStartApplication() - main application launch
// - Implement GetFirmwareMagic() and GetFirmwareValidStatus() - firmware validation parameters
// - Implement OnEraseIteration() - erase one memory quantum
// - Implement OnWriteMemory() - write data to memory
// - Implement OnReadMemory() - read data from memory
// - Optional: OnEncryptData/OnDecryptData for encryption
// - Optional: OnUnlockDevice for protection removal
//
// TEMPLATE PARAMETERS:
// RxBufferSize - receive data buffer size (default 1024)
// AccumBufferSize - Write accumulation buffer size (default 512)
template<size_t RxBufferSize = 1024, size_t AccumBufferSize = 512>
class IBootloader {
public:
    // User data handler function type
    // Parameters: operation (0=read, 1=write), dataType, data
    // Returns: for read - data to send back, for write - success status
    using UserDataHandler = std::function<std::span<const uint8>(uint8 operation, uint8 dataType, std::span<const uint8> data)>;


    // Bootloader commands
    enum class Command : uint8 {
        Ping = 0x01,         // Connection check
        GetInfo = 0x02,      // Get device information
        Erase = 0x10,        // Erase memory region
        Write = 0x11,        // Write data to memory
        Read = 0x12,         // Read data from memory
        Verify = 0x13,       // Verify firmware integrity
        Finalize = 0x14,     // Finalize firmware (set valid status)
        Start = 0x20,        // Start application
        Reset = 0x21,        // Reset device
        GetStatus = 0x30,    // Get current status
        UnlockDevice = 0x40, // Remove RDP protection (requires password)
        UserData = 0x50      // Read/write user data (battery, IV*, etc.)
    };

    // Response statuses
    enum class BootloaderStatus : uint8 {
        Success = 0x00,          // Command executed successfully
        Error = 0x01,            // General execution error
        InvalidCommand = 0x02,   // Unknown command
        InvalidParameters = 0x03,// Invalid command parameters
        Busy = 0x04,             // Device is busy
        NotReady = 0x05,         // Device not ready
        Protected = 0x06,        // Memory protected from write/read
        CryptoError = 0x07,      // Encryption/decryption error
        AccessDenied = 0x08      // Access denied (RDP Level 2 or other protection)
    };

    // Simplified firmware header - only CRC32 and size
    // PURPOSE: minimal structure for firmware validation
    // LOCATION: at application memory start address (e.g. 0x08020000)
    // LOGIC: if size is correct and CRC32 matches - firmware is valid
    struct FirmwareHeader {
        uint32 crc32;        // CRC32 checksum of all firmware data (after this header)
        uint32 size;         // Firmware size in bytes (excluding this header)
    } _APacked;

    // Structure for write command with encryption
    struct EncryptedWriteHeader {
        uint32 address;          // Write address
        uint16 realSize;         // Real data size (before padding)
        uint16 paddedSize;       // Size with padding for encryption
    } _APacked;

    // Structure for user data command
    struct UserDataPacket {
        uint8 operation;         // 0 = Read, 1 = Write
        uint8 dataType;          // Data type identifier (battery=1, IV=2, etc.)
        uint8 length;            // Data length (for write operations)
        uint8 reserved;          // Alignment
        std::array<uint8, 236> data; // User data
    } _APacked;

    // Command packet
    struct CommandPacket {
        uint8 header = 0xAA;     // Packet start marker
        uint8 sequence;          // Packet sequence number
        Command command;         // Command code
        uint8 length;            // Data length in packet
        std::array<uint8, 240> data; // Command data
    } _APacked;

    // Response packet
    struct ResponsePacket {
        uint8 header = 0xBB;     // Response start marker
        uint8 sequence;          // Echo of sequence number
        BootloaderStatus status;           // Command execution status
        uint8 length;            // Response data length
        std::array<uint8, 240> data; // Response data
    } _APacked;


protected:
    // Communication interface
    ICommunication& communication;
    
    // Buffer for receiving data
    RingBuffer<uint8, RxBufferSize> receiveBuffer;
    
    // Buffer for accumulating Write command data
    std::array<uint8, AccumBufferSize> accumulationBuffer;
    size_t accumulatedSize = 0;
    uint32 currentWriteAddress = 0;
    
    // Current state
    enum class State {
        Idle,       // Waiting for commands
        Receiving,  // Receiving data
        Processing, // Processing command
        Error       // Error state
    } state = State::Idle;
    
    // State for iterative operations
    struct AsyncOperation {
        enum Type {
            None,
            Erasing,
            Writing,
            Verifying
        } type = None;
        
        uint32 address = 0;      // Current address
        uint32 targetAddress = 0; // Target address
        uint32 size = 0;         // Total operation size
        uint32 processed = 0;    // How much already processed
        uint32 iteration = 0;    // Iteration number
        uint8 sequence = 0;      // Sequence for response
    } asyncOp;
    
    // Sequence counter
    uint8 currentSequence = 0;
    
    // Firmware parameters
    uint32 firmwareSize = 0;    // Total firmware size
    uint32 firmwareCrc = 0;     // Firmware CRC
    uint32 bytesReceived = 0;   // Number of bytes received (before decryption)
    uint32 bytesWritten = 0;    // Number of bytes actually written (after decryption)
    bool isFirstWrite = true;   // First write flag for header creation
    
    // User data handler
    UserDataHandler userDataHandler;
    

public:
    IBootloader(ICommunication& comm) : communication(comm) {
        communication.SetDataReceivedCallback(
            [this](std::span<const uint8> data) {
                OnDataReceived(data);
            }
        );
    }
    

    virtual ~IBootloader() = default;
    


    virtual Status::statusType Initialize() {
        // Check if we need to enter bootloader mode
        if (!OnCheckBootloaderRequest()) {
            // User doesn't want to enter bootloader
            // Try to start the application
            if (IsValidFirmware() && StartApplication()) {
                // Application started, should not return here
                return Status::error;
            }
            // If application didn't start, continue with bootloader
        }
        
        // Check access protection level
        auto accessStatus = OnCheckAccessLevel();
        if (accessStatus != Status::ok) {
            return accessStatus;
        }
        
        auto status = communication.Initialize();
        if (status != Status::ok) {
            return status;
        }
        
        state = State::Idle;
        return OnInitialize();
    }
    


    virtual void Process() {
        // First process communication (poll NRF8001, DMA, etc)
        communication.Process();
        
        // Then process incoming packets
        ProcessReceivedPackets();
        
        // Additional processing in inheritors
        OnProcess();
    }
    


    virtual bool StartApplication() {
        if (!IsValidFirmware()) {
            return false;
        }
        
        return OnStartApplication();
    }
    


    virtual bool IsValidFirmware() const {
        return CheckFirmware();
    }

    // Set user data handler
    void SetUserDataHandler(UserDataHandler handler) {
        userDataHandler = handler;
    }


protected:
    // Hooks for inheritors
    
    // Additional initialization
    // Access level check (RDP, Flash protection)
    // INHERITOR MUST:
    // - Check RDP protection level
    // - Return Status::accessError if Level 2 (permanent protection)
    // - Optionally warn about Level 1 (limited access)
    // DEFAULT: allows access (for compatibility)
    virtual Status::statusType OnCheckAccessLevel() { return Status::ok; }
    


    // Additional initialization after checks
    // INHERITOR MAY:
    // - Initialize additional hardware
    // - Configure watchdog
    // - Check power supply voltage
    virtual Status::statusType OnInitialize() { return Status::ok; }
    


    // Additional processing in main loop
    // INHERITOR MAY:
    // - Update watchdog
    // - Check timeouts
    // - Handle LED indication
    // IMPORTANT: don't override if not needed - base class already calls ProcessAsyncOperation()
    virtual void OnProcess() { 
        // Process asynchronous operations
        ProcessAsyncOperation();
    }
    


    // Get current protection level (optional)
    // INHERITOR MAY:
    // - Return current RDP protection level for display in GetInfo
    // DEFAULT: not implemented
    virtual std::optional<uint8> GetProtectionLevel() const { return std::nullopt; }
    


    // Start main application
    // INHERITOR MUST:
    // - Check application entry point validity
    // - Deinitialize bootloader peripherals
    // - Configure VTOR to application address
    // - Set Stack Pointer
    // - Transfer control to application
    // - Return false (should not return here)
    virtual bool OnStartApplication() = 0;
    


    // Check for valid firmware in memory
    // USED: at startup to decide whether to run application or bootloader
    // IMPLEMENTATION: unified logic in IBootloader, uses OnReadMemory()
    bool CheckFirmware() const {
        // Read firmware header
        FirmwareHeader header;
        uint8* headerPtr = reinterpret_cast<uint8*>(&header);
        
        // Use inheritor's high-level read method
        auto status = const_cast<IBootloader*>(this)->OnReadMemory(
            GetMemoryStartAddress(), 
            std::span<uint8>(headerPtr, sizeof(FirmwareHeader))
        );
        
        if (status != Status::ok) {
            return false;
        }
        
        // Check size - the only boundary check
        if (header.size == 0 || header.size > GetMemorySize() - sizeof(FirmwareHeader)) {
            return false;
        }
        
        // PROTECTION from typical Flash garbage
        if (header.size == 0xFFFFFFFF || header.crc32 == 0xFFFFFFFF) {
            return false;  // All bits set - erased Flash
        }
        
        // Additional validation from inheritor
        return OnValidateFirmware(header);
    }



    // Additional firmware validation (optional)
    // INHERITOR MAY override to check digital signature
    // DEFAULT: checks only CRC32 - this is sufficient
    virtual bool OnValidateFirmware(const FirmwareHeader& header) const { 
        // PROTECTION: check that header.size matches actually written data
        // BUT only if firmware was written in this session (bytesWritten > 0)
        if (bytesWritten > 0 && header.size != bytesWritten) {
            return false;  // Header size doesn't match actually written data in this session
        }
        
        // ALWAYS check CRC32 - this is the only integrity check
        uint32 calculatedCrc = CalculateFirmwareCRC32();
        if (calculatedCrc == 0) {
            return false;  // CRC32 calculation error
        }
        
        // header.crc32 == 0 means header is corrupted or not finalized
        if (header.crc32 == 0 || header.crc32 != calculatedCrc) {
            return false;  // CRC32 doesn't match, corrupted or not set
        }
        
        return true; 
    }
    


    // Firmware finalization - set valid status
    // INHERITOR MAY override:
    // - Write FirmwareHeader.status = validStatus
    // - Set readiness flags
    // - Update CRC32 if needed
    // DEFAULT: does nothing (for compatibility)
    virtual Status::statusType OnFinalizeFirmware() { return Status::ok; }
    


    // Check if bootloader mode is requested
    // INHERITOR MUST check:
    // - Button state (if present)
    // - Special flag in RAM (set by application)
    // - Watchdog reset cause
    // - Other device-specific conditions
    // RETURNS:
    // - true: enter bootloader mode
    // - false: try to start application
    virtual bool OnCheckBootloaderRequest() const = 0;
    


    // Memory parameters
    // INHERITOR MUST return:
    // - Start address where application can be located (usually after bootloader)
    // - For STM32: typically 0x08008000 or 0x08010000
    // - For ESP32: address in external Flash
    virtual uint32 GetMemoryStartAddress() const = 0;
    


    // INHERITOR MUST return:
    // - Maximum application size in bytes
    // - Consider that part of Flash is occupied by bootloader
    virtual uint32 GetMemorySize() const = 0;
    


    // End memory address (calculated automatically)
    virtual uint32 GetMemoryEndAddress() const {
        return GetMemoryStartAddress() + GetMemorySize();
    }
    


    // Start address of actual application code (after firmware header)
    virtual uint32 GetApplicationStartAddress() const {
        return GetMemoryStartAddress() + sizeof(FirmwareHeader);
    }
    
    

    // Memory protection check (RDP - Read Protection)
    // INHERITOR MAY override if supports protection:
    // - For STM32: check RDP level through Option Bytes
    // - For ESP32: check eFuse bits
    // - Default assumes no protection
    virtual bool IsMemoryProtected() const { return false; }
    


    // Memory range validation
    bool IsValidMemoryRange(uint32 address, uint32 size) const {
        if (size == 0) return false;
        if (address < GetMemoryStartAddress()) return false;
        if (address + size > GetMemoryEndAddress()) return false;
        if (address + size < address) return false; // Overflow check
        return true;
    }
    


    // Encryption block size (AES uses 16 bytes)
    static constexpr size_t CRYPTO_BLOCK_SIZE = 16;
    


    // Align size to blocks for encryption
    size_t AlignToBlockSize(size_t size) const {
        return ((size + CRYPTO_BLOCK_SIZE - 1) / CRYPTO_BLOCK_SIZE) * CRYPTO_BLOCK_SIZE;
    }
    


    // Hooks for data handling and encryption
    
    // Check if there's enough data for processing
    // USED: when accumulating Write command data
    // INHERITOR MAY override for:
    // - AES ECB/CBC: check that data >= 16 bytes (block size)
    // - Other block ciphers: check block size multiplicity
    // - Default: any amount of data is sufficient
    virtual bool OnHasEnoughData(std::span<const uint8> buffer) const {
        return buffer.size() > 0;
    }
    


    // How many bytes to take from buffer for processing
    // USED: to determine portion size for decryption
    // INHERITOR MAY return:
    // - For AES ECB/CBC: size multiple of 16 (whole blocks)
    // - For stream ciphers: any size
    // - Default: all available data
    virtual size_t OnGetRequiredBytes(std::span<const uint8> buffer) const {
        return buffer.size();
    }
    


    // Data encryption (may change size)
    // USED: for Read command to encrypt response
    // INHERITOR MUST:
    // - Encrypt data in-place or in own buffer
    // - Return span with encrypted data
    // - Return empty span on encryption error
    // - Buffer must remain valid until sent
    // EXAMPLES:
    // - AES: use internal buffer, return span to it
    // - XOR: encrypt in-place, return original span
    virtual std::span<uint8> OnEncryptData(std::span<uint8> data) { 
        return data;  // Default without encryption
    }
    


    // Data decryption (may change size)
    // USED: for Write command to decrypt data
    // INHERITOR MUST:
    // - Decrypt data in-place or in own buffer
    // - Return span with decrypted data
    // - Return empty span on decryption error
    // - Buffer must remain valid until written
    // IMPORTANT: size may change (remove padding)
    virtual std::span<uint8> OnDecryptData(std::span<uint8> data) { 
        return data;  // Default without encryption
    }
    


    // Hooks for data read/write
    
    // Write data to memory
    // INHERITOR MUST:
    // - Check that address is aligned if required (usually by 4 bytes)
    // - Unlock Flash before writing
    // - Write data (consider that Flash is written in words)
    // - Lock Flash after writing
    // - Return Status::ok only if ALL data written successfully
    // IMPORTANT: data is already decrypted (if encryption was used)
    virtual Status::statusType OnWriteMemory(uint32 address, std::span<const uint8> data) = 0;
    


    // Read data from memory
    // INHERITOR MUST:
    // - Read exactly data.size() bytes starting from address
    // - Fill data buffer with read data
    // - Return Status::ok if read successfully
    // IMPORTANT: data will be encrypted after (if encryption is enabled)
    virtual Status::statusType OnReadMemory(uint32 address, std::span<uint8> data) = 0;
    


    // Iterative erase - one quantum of work
    // PURPOSE: allow partial memory erase to not block the system
    //
    // PARAMETERS:
    // address - current address from which to start erasing
    // endAddress - final address to erase to
    // iteration - iteration number (0, 1, 2...) - inheritor may use or ignore
    // bytesErased - [out] INHERITOR MUST write here how many bytes were erased this iteration
    //
    // INHERITOR MUST:
    // - Erase PART of memory (e.g. one sector)
    // - Set bytesErased = number of erased bytes
    // - Return Status::ok if erased part (not all yet)
    // - Return Status::complete if erased everything up to endAddress
    // - Return Status::error on error
    //
    // EXAMPLES:
    // - Fast Flash: erase all at once, return complete
    // - STM32: erase one sector, return ok or complete
    // - BLE optimization: erase while < 20ms elapsed
    virtual Status::statusType OnEraseIteration(uint32 address, uint32 endAddress, uint32 iteration, uint32& bytesErased) = 0;


    
    // Hook for device unlocking (RDP removal)
    // password - password to check unlock rights
    virtual Status::statusType OnUnlockDevice(std::span<const uint8> password) {
        // Not supported by default
        return Status::notSupported;
    }
    


    // Process received data
    void OnDataReceived(std::span<const uint8> data) {
        // Add data to buffer
        for (uint8 byte : data) {
            receiveBuffer.Push(byte);
        }
    }
    


    void ProcessReceivedPackets() {
        while (TryProcessPacket()) {
        }
    }
    


    bool TryProcessPacket() {
        if (receiveBuffer.Size() < sizeof(CommandPacket)) {
            return false;
        }
        
        // Look for packet header
        while (receiveBuffer.Size() > 0) {
            auto peek = receiveBuffer.Peek();
            if (peek.IsOk() && peek.data != 0xAA) {
                receiveBuffer.Pop();
            } else {
                break;
            }
        }
        
        if (receiveBuffer.Size() < sizeof(CommandPacket)) {
            return false;
        }
        
        // Read packet
        CommandPacket packet;
        if (!ReadPacket(packet)) {
            return false;
        }
        
        // Check CRC
        if (!ValidatePacket(packet)) {
            SendError(packet.sequence, BootloaderStatus::InvalidParameters);
            return true;
        }
        
        // Process command
        ProcessCommand(packet);
        return true;
    }
    


    bool ReadPacket(CommandPacket& packet) {
        // Check size
        if (receiveBuffer.Size() < 4) {
            return false;
        }
        
        // Read header to determine size
        packet.header = receiveBuffer[0];
        packet.sequence = receiveBuffer[1];
        packet.command = static_cast<Command>(receiveBuffer[2]);
        packet.length = receiveBuffer[3];
        
        size_t totalSize = 4 + packet.length; // header + seq + cmd + len + data
        if (receiveBuffer.Size() < totalSize) {
            return false;
        }
        
        // Extract entire packet
        for (size_t i = 0; i < totalSize; i++) {
            auto result = receiveBuffer.Pop();
            if (result.IsOk()) {
                reinterpret_cast<uint8*>(&packet)[i] = result.data;
            } else {
                return false;
            }
        }
        
        return true;
    }
    


    bool ValidatePacket(const CommandPacket& packet) {
        // Check header only
        // CRC and other integrity checks are done by transport layer
        return packet.header == 0xAA;
    }
    


    void ProcessCommand(const CommandPacket& packet) {
        currentSequence = packet.sequence;
        
        switch (packet.command) {
            case Command::Ping:
                HandlePing();
                break;
                
            case Command::GetInfo:
                HandleGetInfo();
                break;
                
            case Command::Erase:
                HandleErase(packet);
                break;
                
            case Command::Write:
                HandleWrite(packet);
                break;
                
            case Command::Read:
                HandleRead(packet);
                break;
                
            case Command::Verify:
                HandleVerify();
                break;
                
            case Command::Finalize:
                HandleFinalize();
                break;
                
            case Command::Start:
                HandleStart();
                break;
                
            case Command::Reset:
                HandleReset();
                break;
                
            case Command::GetStatus:
                HandleGetStatus();
                break;
                
            case Command::UnlockDevice:
                HandleUnlockDevice(packet);
                break;
                
            case Command::UserData:
                HandleUserData(packet);
                break;
                
            default:
                SendError(packet.sequence, BootloaderStatus::InvalidCommand);
                break;
        }
    }
    


    // Command handlers
    void HandlePing() {
        SendResponse(BootloaderStatus::Success);
    }
    


    void HandleGetInfo() {
        struct DeviceInfo {
            uint32 bootloaderVersion = 0x00010000;
            uint32 maxPacketSize;
            uint32 memoryStart;
            uint32 memorySize;
            uint8 protectionLevel;  // RDP level: 0 = Level0, 1 = Level1, 2 = Level2, 255 = Unknown
            uint8 accessDenied;     // Access status: 0 = allowed, 1 = denied
            uint8 reserved[2];      // Alignment
        } info;
        
        info.maxPacketSize = communication.GetMaxPacketSize();
        info.memoryStart = GetMemoryStartAddress();
        info.memorySize = GetMemorySize();
        
        // Check access level and protection
        auto accessStatus = OnCheckAccessLevel();
        info.accessDenied = (accessStatus == Status::accessError) ? 1 : 0;
        
        // Map protection level to numeric value
        if (auto level = GetProtectionLevel(); level.has_value()) {
            switch (*level) {
                case 0: info.protectionLevel = 0; break;  // Level0
                case 1: info.protectionLevel = 1; break;  // Level1  
                case 2: info.protectionLevel = 2; break;  // Level2
                default: info.protectionLevel = 255; break; // Unknown
            }
        } else {
            info.protectionLevel = 255; // Unknown/Not implemented
        }
        
        info.reserved[0] = info.reserved[1] = 0;
        
        SendResponse(BootloaderStatus::Success, std::span(reinterpret_cast<const uint8*>(&info), sizeof(info)));
    }
    


    void HandleErase(const CommandPacket& packet) {
        if (state == State::Processing || asyncOp.type != AsyncOperation::None) {
            SendError(currentSequence, BootloaderStatus::Busy);
            return;
        }
        
        // Check access level (RDP Level 2 blocks Flash operations)
        auto accessStatus = OnCheckAccessLevel();
        if (accessStatus == Status::accessError) {
            SendError(currentSequence, BootloaderStatus::AccessDenied);
            return;
        }
        
        // Check memory protection
        if (IsMemoryProtected()) {
            SendError(currentSequence, BootloaderStatus::Protected);
            return;
        }
        
        if (packet.length < 8) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        uint32 address = *reinterpret_cast<const uint32*>(&packet.data[0]);
        uint32 size = *reinterpret_cast<const uint32*>(&packet.data[4]);
        
        // Check address range
        if (!IsValidMemoryRange(address, size)) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        // Start asynchronous erase
        asyncOp.type = AsyncOperation::Erasing;
        asyncOp.address = address;
        asyncOp.targetAddress = address + size;
        asyncOp.size = size;
        asyncOp.processed = 0;
        asyncOp.iteration = 0;
        asyncOp.sequence = packet.sequence;
        
        // Reset accumulators on erase
        accumulatedSize = 0;
        bytesReceived = 0;
        bytesWritten = 0;
        isFirstWrite = true;  // Next write will be first
        
        state = State::Processing;
        
        // Immediately return acknowledgment that command is accepted
        // Final response will be sent after operation completes
    }
    


    void HandleWrite(const CommandPacket& packet) {
        if (state == State::Processing) {
            SendError(currentSequence, BootloaderStatus::Busy);
            return;
        }
        
        // Check access level (RDP Level 2 blocks Flash operations)
        auto accessStatus = OnCheckAccessLevel();
        if (accessStatus == Status::accessError) {
            SendError(currentSequence, BootloaderStatus::AccessDenied);
            return;
        }
        
        // Check memory protection
        if (IsMemoryProtected()) {
            SendError(currentSequence, BootloaderStatus::Protected);
            return;
        }
        
        // Simple format: address(4) + data
        if (packet.length < 4) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        uint32 address = *reinterpret_cast<const uint32*>(&packet.data[0]);
        size_t dataSize = packet.length - 4;
        
        // If this is first packet or new address - reset accumulator
        if (accumulatedSize == 0 || address != currentWriteAddress + accumulatedSize) {
            accumulatedSize = 0;
            currentWriteAddress = address;
        }
        
        // Check that we won't overflow the buffer
        if (accumulatedSize + dataSize > accumulationBuffer.size()) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        // Copy new data to accumulator
        std::copy(packet.data.begin() + 4, packet.data.begin() + 4 + dataSize, 
                  accumulationBuffer.begin() + accumulatedSize);
        accumulatedSize += dataSize;
        
        // Process accumulated data
        state = State::Processing;
        bool success = ProcessAccumulatedData();
        state = State::Idle;
        
        if (success) {
            SendResponse(BootloaderStatus::Success);
        } else {
            // If we're in Error state, it's most likely a cryptographic error
            BootloaderStatus errorStatus = (state == State::Error) ? BootloaderStatus::CryptoError : BootloaderStatus::Error;
            SendError(currentSequence, errorStatus);
        }
    }
    


    void HandleRead(const CommandPacket& packet) {
        if (state == State::Processing) {
            SendError(currentSequence, BootloaderStatus::Busy);
            return;
        }
        
        // Check memory protection
        if (IsMemoryProtected()) {
            SendError(currentSequence, BootloaderStatus::Protected);
            return;
        }
        
        if (packet.length < 8) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        uint32 address = *reinterpret_cast<const uint32*>(&packet.data[0]);
        uint32 size = *reinterpret_cast<const uint32*>(&packet.data[4]);
        
        if (size > 240) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        // Check address range
        if (!IsValidMemoryRange(address, size)) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        state = State::Processing;
        
        std::array<uint8, 240> readData{};
        std::span<uint8> dataSpan(readData.data(), size);
        
        auto status = OnReadMemory(address, dataSpan);
        if (status == Status::ok) {
            // Encrypt data (may change size)
            auto encryptedData = OnEncryptData(dataSpan);
            
            // Check that encryption was successful
            if (encryptedData.empty()) {
                SendError(currentSequence, BootloaderStatus::CryptoError);
            }
            // Check that encrypted data will fit in response
            else if (encryptedData.size() <= 240) {
                SendResponse(BootloaderStatus::Success, encryptedData);
            } else {
                SendError(currentSequence, BootloaderStatus::Error);
            }
        } else {
            SendError(currentSequence, BootloaderStatus::Error);
        }
        
        state = State::Idle;
    }
    


    void HandleVerify() {
        if (IsValidFirmware()) {
            SendResponse(BootloaderStatus::Success);
        } else {
            SendError(currentSequence, BootloaderStatus::Error);
        }
    }
    


    void HandleFinalize() {
        // Check access
        auto accessStatus = OnCheckAccessLevel();
        if (accessStatus == Status::accessError) {
            SendError(currentSequence, BootloaderStatus::AccessDenied);
            return;
        }
        
        // Finalize firmware
        auto status = OnFinalizeFirmware();
        if (status == Status::ok) {
            SendResponse(BootloaderStatus::Success);
        } else {
            SendError(currentSequence, BootloaderStatus::Error);
        }
    }
    


    void HandleStart() {
        if (StartApplication()) {
            SendResponse(BootloaderStatus::Success);
            // Application should start
        } else {
            SendError(currentSequence, BootloaderStatus::Error);
        }
    }
    


    void HandleReset() {
        SendResponse(BootloaderStatus::Success);
        System::DelayMs(100);
        NVIC_SystemReset();
    }
    


    void HandleGetStatus() {
        struct StatusInfo {
            uint8 state;
            uint32 bytesReceived;
        } status;
        
        status.state = static_cast<uint8>(state);
        status.bytesReceived = bytesReceived;
        
        SendResponse(BootloaderStatus::Success,
                    std::span(reinterpret_cast<const uint8*>(&status), sizeof(status)));
    }
    
    
    
    void HandleUnlockDevice(const CommandPacket& packet) {
        // Check memory protection
        if (!IsMemoryProtected()) {
            // Already unlocked
            SendResponse(BootloaderStatus::Success);
            return;
        }
        
        // Packet must contain password for unlocking
        if (packet.length < 16) {  // Minimum 16 bytes for password
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        // Check password and unlock device
        std::span<const uint8> password(packet.data.data(), packet.length);
        auto status = OnUnlockDevice(password);
        
        if (status == Status::ok) {
            SendResponse(BootloaderStatus::Success);
            // After unlock, reboot may be required
        } else {
            SendError(currentSequence, BootloaderStatus::Error);
        }
    }
    
    
    
    void HandleUserData(const CommandPacket& packet) {
        // Check if handler is set
        if (!userDataHandler) {
            SendError(currentSequence, BootloaderStatus::InvalidCommand);
            return;
        }
        
        // Packet must contain at least operation and dataType
        if (packet.length < 2) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        uint8 operation = packet.data[0];  // 0=read, 1=write
        uint8 dataType = packet.data[1];   // data type identifier
        
        std::span<const uint8> inputData;
        if (operation == 1 && packet.length > 2) {  // Write operation
            inputData = std::span<const uint8>(packet.data.data() + 2, packet.length - 2);
        }
        
        // Call user handler
        auto result = userDataHandler(operation, dataType, inputData);
        
        if (result.empty() && operation == 0) {
            // Read operation but no data returned - error
            SendError(currentSequence, BootloaderStatus::Error);
        } else if (result.size() > 240) {
            // Response too large
            SendError(currentSequence, BootloaderStatus::Error);
        } else {
            // Success - send data back (empty for write operations is OK)
            SendResponse(BootloaderStatus::Success, result);
        }
    }
    


    void SendResponse(BootloaderStatus status, std::span<const uint8> data = {}) {
        ResponsePacket response{};
        response.sequence = currentSequence;
        response.status = status;
        response.length = static_cast<uint8>(data.size());
        
        if (!data.empty()) {
            std::copy(data.begin(), data.end(), response.data.begin());
        }
        
        // Send entire packet
        // CRC and other checks will be added by transport layer
        communication.SendData(std::span(
            reinterpret_cast<const uint8*>(&response),
            offsetof(ResponsePacket, data) + response.length
        ));
    }
    


    void SendError(uint8 sequence, BootloaderStatus status) {
        currentSequence = sequence;
        SendResponse(status);
    }
    


    bool CreateInitialFirmwareHeader() {
        FirmwareHeader header{};
        header.crc32 = 0; // Will be updated in Finalize
        header.size = 0;  // Will be updated in Finalize
        
        // Write header to beginning of memory
        auto status = OnWriteMemory(GetMemoryStartAddress(), std::span<const uint8>(reinterpret_cast<const uint8*>(&header), sizeof(header)));
        return status == Status::ok;
    }



    // Calculate CRC32 of all firmware data (after header)
    uint32 CalculateFirmwareCRC32() const {
        uint32 firmwareDataSize;
        
        // If firmware was written in this session - use bytesWritten
        if (bytesWritten > 0) {
            firmwareDataSize = bytesWritten;
        } else {
            // For old firmware (after reboot) read size from header
            FirmwareHeader header;
            uint8* headerPtr = reinterpret_cast<uint8*>(&header);
            
            auto status = const_cast<IBootloader*>(this)->OnReadMemory(
                GetMemoryStartAddress(), 
                std::span<uint8>(headerPtr, sizeof(FirmwareHeader))
            );
            
            if (status != Status::ok) {
                return 0;  // Header read error
            }
            
            firmwareDataSize = header.size;
        }
        
        if (firmwareDataSize == 0) {
            return 0;  // No firmware data
        }
        
        // PROTECTION: check that size doesn't exceed available memory
        uint32 maxFirmwareSize = GetMemorySize() - sizeof(FirmwareHeader);
        if (firmwareDataSize > maxFirmwareSize) {
            return 0;  // Invalid size - possible data corruption
        }
        
        // Read firmware data in chunks and calculate CRC32
        const uint32 CHUNK_SIZE = 256;  // Read 256 bytes at a time
        std::array<uint8, CHUNK_SIZE> buffer;
        uint32 currentCrc = 0;
        uint32 firmwareStart = GetMemoryStartAddress() + sizeof(FirmwareHeader);
        
        for (uint32 offset = 0; offset < firmwareDataSize; offset += CHUNK_SIZE) {
            uint32 chunkSize = std::min(CHUNK_SIZE, firmwareDataSize - offset);
            std::span<uint8> chunkSpan(buffer.data(), chunkSize);
            
            // PROTECTION: check read boundaries before calling OnReadMemory
            uint32 readAddress = firmwareStart + offset;
            if (!IsValidMemoryRange(readAddress, chunkSize)) {
                return 0;  // Attempted read beyond memory boundaries
            }
            
            auto readStatus = const_cast<IBootloader*>(this)->OnReadMemory(
                readAddress, chunkSpan
            );
            
            if (readStatus != Status::ok) {
                return 0;  // Read error
            }
            
            // Calculate CRC32 incrementally
            if (offset == 0) {
                // First chunk
                currentCrc = Crc::Calculate<uint32, 32>(buffer.data(), chunkSize, Crc::CRC_32());
            } else {
                // Continue with previous CRC
                currentCrc = Crc::Calculate<uint32, 32>(buffer.data(), chunkSize, Crc::CRC_32(), currentCrc);
            }
        }
        
        return currentCrc;
    }



    // Process accumulated data
    // Returns true if processing was successful, false on error
    bool ProcessAccumulatedData() {
        // If this is first write to start address - create header
        if (isFirstWrite && currentWriteAddress == GetMemoryStartAddress()) {
            if (!CreateInitialFirmwareHeader()) {
                state = State::Error;
                return false;
            }
            isFirstWrite = false;
        }
        
        size_t processedBytes = 0;
        
        while (processedBytes < accumulatedSize) {
            // Check if there's enough data for processing
            std::span<const uint8> remainingData(
                accumulationBuffer.data() + processedBytes,
                accumulatedSize - processedBytes
            );
            
            if (!OnHasEnoughData(remainingData)) {
                // Not enough data, shift remainder to beginning of buffer
                break;
            }
            
            // Determine how many bytes to process
            size_t bytesToProcess = OnGetRequiredBytes(remainingData);
            if (bytesToProcess == 0 || bytesToProcess > remainingData.size()) {
                // Error in inheritor logic
                state = State::Error;
                return false;
            }
            
            // Prepare data for decryption
            std::span<uint8> dataToDecrypt(
                accumulationBuffer.data() + processedBytes,
                bytesToProcess
            );
            
            // Decrypt (may return different size)
            auto decryptedData = OnDecryptData(dataToDecrypt);
            
            // Check that decryption was successful
            if (decryptedData.empty()) {
                // Decryption error - this is critical error
                state = State::Error;
                return false;
            }
            
            // Calculate write address (with header offset if writing to start)
            uint32 writeAddress = currentWriteAddress + bytesWritten;
            if (currentWriteAddress == GetMemoryStartAddress()) {
                writeAddress += sizeof(FirmwareHeader);  // Shift after header
            }
            
            // Check address validity for writing
            if (!IsValidMemoryRange(writeAddress, decryptedData.size())) {
                state = State::Error;
                return false;
            }
            
            // Write decrypted data
            auto status = OnWriteMemory(writeAddress, decryptedData);
            if (status != Status::ok) {
                state = State::Error;
                return false;
            }
            
            bytesWritten += decryptedData.size();  // Real data (after decryption)
            processedBytes += bytesToProcess;
        }
        
        // Shift unused data to beginning of buffer
        if (processedBytes < accumulatedSize) {
            size_t remainingBytes = accumulatedSize - processedBytes;
            std::copy(accumulationBuffer.begin() + processedBytes,
                     accumulationBuffer.begin() + accumulatedSize,
                     accumulationBuffer.begin());
            accumulatedSize = remainingBytes;
        } else {
            accumulatedSize = 0;
        }
        
        return true;  // Successful processing
    }



    void ProcessAsyncOperation() {
        if (asyncOp.type == AsyncOperation::None) {
            return;
        }
        
        switch (asyncOp.type) {
            case AsyncOperation::Erasing:
                ProcessAsyncErase();
                break;
                
            case AsyncOperation::Writing:
                break;
                
            case AsyncOperation::Verifying:
                break;
                
            default:
                break;
        }
    }



    void ProcessAsyncErase() {
        // Call iterative erase
        uint32 bytesErased = 0;
        auto status = OnEraseIteration(
        	asyncOp.address + asyncOp.processed,
			asyncOp.targetAddress,
			asyncOp.iteration,
			bytesErased
		);
        
        if (status == Status::error) {
            // Erase error
            asyncOp.type = AsyncOperation::None;
            state = State::Idle;
            SendError(asyncOp.sequence, BootloaderStatus::Error);
            return;
        }
        
        // Update progress
        asyncOp.processed += bytesErased;
        asyncOp.iteration++;
        
        // Check completion
        if (status == Status::ok || asyncOp.processed >= asyncOp.size) {
            // Erase completed
            asyncOp.type = AsyncOperation::None;
            state = State::Idle;
            currentSequence = asyncOp.sequence;
            SendResponse(BootloaderStatus::Success);
        }
        // Otherwise continue in next Process()
    }
};
