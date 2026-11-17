#pragma once
#include <VHAL.h>
#include "ICommunication.h"
#include <Utilities/Buffer/RingBuffer.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Crypto/SHA/SHA256.h>
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <span>

// Abstract bootloader class with base protocol logic
template<size_t RxBufferSize = 1024, size_t AccumBufferSize = 512, size_t PacketDataMaxSize = 240>
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
        ResetWritePos = 0x15, // Reset write position to 0
        ResetReadPos = 0x16,  // Reset read position to 0
        GetHash = 0x17,      // Get SHA256 hash of raw data sent so far
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
        AccessDenied = 0x08,     // Access denied (RDP Level 2 or other protection)
        EndOfData = 0x09,        // No more data to read (for READ command)
        ChecksumMismatch = 0x0A  // Image in flash does not match the checksum sent by the host
    };

    // Simplified firmware header - only CRC32 and size
    // PURPOSE: minimal structure for firmware validation
    // LOCATION: at application memory start address (e.g. 0x08020000)
    // LOGIC: if size is correct and CRC32 matches - firmware is valid
    struct FirmwareHeader {
        uint32 crc32;        // CRC32 checksum of all firmware data (after this header)
        uint32 size;         // Firmware size in bytes (excluding this header)
    } _APacked;

    // Command packet
    struct CommandPacket {
        uint8 header = 0xAA;     // Packet start marker
        uint8 sequence;          // Packet sequence number
        Command command;         // Command code
        uint8 length;            // Data length in packet
        std::array<uint8, PacketDataMaxSize> data; // Command data
    } _APacked;

    // Response packet
    struct ResponsePacket {
        uint8 header = 0xBB;     // Response start marker
        uint8 sequence;          // Echo of sequence number
        BootloaderStatus status;           // Command execution status
        uint8 length;            // Response data length
        std::array<uint8, PacketDataMaxSize> data; // Response data
    } _APacked;

    bool checkFirmware = true;

    // Partial packet timeout in ms (0 disables). Measured from the moment an incomplete packet reaches the front of the
    // receive buffer; its header byte is dropped once this time has passed. Must be longer than the transport needs for
    // one full packet including its retries (CompactStreamingProtocol retries a fragment after 5 s)
    uint32 packetTimeoutMs = 10000;

    // Finalize must carry the expected FirmwareHeader. false allows legacy hosts that send an empty payload
    bool requireFinalizeChecksum = true;

    // Erase/Write/Finalize while read-out protection is active (IsMemoryProtected(), e.g. RDP Level 1).
    // Off by default: the images are not authenticated (the AES modes encrypt but do not authenticate), so anyone on
    // the transport could install code that reads out the rest of the flash, including the bootloader and its keys.
    // Enable only when the images are authenticated, for example by a signature checked in OnFinalizeFirmware().
    bool allowUpdatesWhenReadProtected = false;

protected:
    // Communication interface
    ICommunication& communication;
    
    // Buffer for receiving data
    RingBuffer<uint8, RxBufferSize> receiveBuffer;
    
    // Buffer for accumulating Write command data
    std::array<uint8, AccumBufferSize> accumulationBuffer;
    size_t accumulatedSize = 0;
    
    // Stream positions for reading and writing
    uint32 writePosition = 0;  // Current position for writing firmware data
    uint32 readPosition = 0;   // Current position for reading firmware data
    
    // Current state
    // Values are sent to the host in GetStatus, do not renumber
    // Receiving and Error are not used by the state machine any more, kept for wire compatibility
    enum class State : uint8 {
        Idle = 0,       // Waiting for commands
        Receiving = 1,  // Kept for wire compatibility
        Processing = 2, // Processing command (erase in progress, write in progress)
        Error = 3       // Kept for wire compatibility
    } state = State::Idle;

    // Outcome of ProcessAccumulatedData(), decides between CryptoError and Error
    enum class ProcessResult : uint8 {
        Success,
        CryptoError,
        Error
    };
    
    // State for iterative operations
    struct AsyncOperation {
        enum Type {
            None,
            Erasing
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
    uint32 bytesReceived = 0;   // Number of bytes received (before decryption)
    uint32 bytesWritten = 0;    // Number of bytes actually written (after decryption)

    // Write stream state
    bool writeStreamFailed = false;                              // A Write failed, every Write answers writeStreamError until Erase/ResetWritePos
    BootloaderStatus writeStreamError = BootloaderStatus::Error; // Status answered while writeStreamFailed is set

    // Last accepted Write, used to answer a retry of the same packet without touching the data
    bool hasLastWriteSequence = false;
    uint8 lastWriteSequence = 0;
    BootloaderStatus lastWriteStatus = BootloaderStatus::Success;

    // Incomplete packet at the front of the receive buffer and the time it was first seen (ms)
    bool partialPacketPending = false;
    uint32 partialPacketSinceMs = 0;

    // Start is answered first and executed after the response had time to leave (0 = no start scheduled)
    uint64 startTime = 0;
    static constexpr uint32 responseFlushDelayMs = 150;

    // One response waiting for the transport to become ready (SendData returned busy)
    bool hasPendingResponse = false;
    ResponsePacket pendingResponse{};
    size_t pendingResponseSize = 0;

    // Read size to advance once the queued Read response is actually sent (0 = no Read is queued)
    uint32 pendingReadAdvance = 0;
    
    // User data handler
    UserDataHandler userDataHandler;
    
    // Reset
    uint64 resetTime = 0;
    
    // Bootloader version
    uint64 bootloaderVersionValue = 0x00010000;
    
    // SHA256 hasher for raw data integrity check
    // Accumulates SHA256 of all raw data sent to Write command (before decryption)
    // Reset on Erase, ResetWritePos operations
    SHA256 dataHasher;


public:
    IBootloader(ICommunication& comm) : communication(comm) {
        communication.SetDataReceivedCallback(
            [this](std::span<const uint8> data) {
                OnDataReceived(data);
            }
        );
    }
    

    virtual ~IBootloader() = default;
    


    virtual ResultStatus Initialize() {
        // Early initialization hook (e.g. for Flash init before checking firmware)
        ResultStatus earlyStatus = OnBeforeInitialize();
        if (earlyStatus != ResultStatus::ok) {
            return earlyStatus;
        }
        
        // Check if we need to enter bootloader mode
        if (!OnCheckBootloaderRequest()) {
            // User doesn't want to enter bootloader
            // Try to start the application
            if (StartApplication()) {
                // Application started, should not return here
                return ResultStatus::error;
            }
            // If application didn't start, continue with bootloader
        }
        
        // When the access level forbids flash changes (e.g. RDP Level 2) the bootloader does not start its transport
        ResultStatus accessStatus = OnCheckAccessLevel();
        if (accessStatus != ResultStatus::ok) {
            return accessStatus;
        }

        ResultStatus status = communication.Initialize();
        if (status != ResultStatus::ok) {
            return status;
        }
        
        state = State::Idle;
        return OnInitialize();
    }
    


    virtual void Process() {
        // 1. Poll communication (NRF8001, DMA, etc)
        communication.Process();
        
        // 2. Send a response that was queued while the transport was busy
        FlushPendingResponse();

        // 3. Incoming packets are handled only while no response is waiting to be sent
        ProcessReceivedPackets();
        
        // 4. Asynchronous work (erase) and deferred actions only when no response is pending
        if (!hasPendingResponse) {
            OnProcess();
        }

        if (!hasPendingResponse && startTime != 0 && System::GetMs() >= startTime) {
            startTime = 0;
            OnStartApplication();
        }

        if (!hasPendingResponse && resetTime != 0 && resetTime < System::GetMs()) {
            System::Reset();
        }
    }
    


    virtual bool StartApplication() {
        if (checkFirmware && !CheckFirmware()) {
            return false;
        }
        
        return OnStartApplication();
    }

    // Set user data handler
    void SetUserDataHandler(UserDataHandler handler) {
        userDataHandler = handler;
    }


    // Set bootloader version
    void SetBootloaderVersion(uint64 version) {
        bootloaderVersionValue = version;
    }


protected:
    // Hooks for inheritors
    
    virtual ResultStatus OnBeforeInitialize() { return ResultStatus::ok; }

    virtual ResultStatus OnCheckAccessLevel() { return ResultStatus::ok; }
    


    virtual ResultStatus OnInitialize() { return ResultStatus::ok; }
    


    virtual void OnProcess() {
        // Process asynchronous operations
        ProcessAsyncOperation();
    }
    


    virtual std::optional<uint8> GetProtectionLevel() const { return std::nullopt; }
    


    virtual bool OnStartApplication() = 0;
    


    bool CheckFirmware() const {
        // Read firmware header from end of memory
        FirmwareHeader header;
        uint8* headerPtr = reinterpret_cast<uint8*>(&header);
        
        // Use inheritor's high-level read method
        ResultStatus status = const_cast<IBootloader*>(this)->OnReadMemory(
            GetFirmwareHeaderAddress(), 
            std::span<uint8>(headerPtr, sizeof(FirmwareHeader))
        );
        
        if (status != ResultStatus::ok) {
            return false;
        }
        
        // Check size - firmware can use all memory except header space at end
        if (header.size == 0 || header.size > GetMemorySize() - sizeof(FirmwareHeader)) {
            return false;
        }
        
        // The size range check above also rejects an erased header (size 0xFFFFFFFF)
        // CRC32 is the integrity check; CRC value 0 is a valid CRC
        std::optional<uint32> calculatedCrc = CalculateFirmwareCRC32(header.size);
        if (!calculatedCrc.has_value()) {
            return false;  // Read or range error while calculating the CRC
        }

        if (header.crc32 != *calculatedCrc) {
            return false;  // CRC32 doesn't match, corrupted or not set
        }
        
        return true; 
    }
    


    virtual ResultStatus OnFinalizeFirmware() {
        // Only finalize if we actually wrote some data
        if (bytesWritten == 0) {
            return ResultStatus::error;
        }
        
        // CRC of the image as it is in flash, over the size written in this session
        std::optional<uint32> crc = CalculateFirmwareCRC32(bytesWritten);
        if (!crc.has_value()) {
            return ResultStatus::error;  // Read or range error
        }
        
        FirmwareHeader header{};
        header.size = bytesWritten;
        header.crc32 = *crc;

        // A repeated Finalize (lost response) finds the same header already in flash: do not program it again,
        // programming a non-erased word fails on flash such as STM32G0/G4
        std::array<uint8, sizeof(FirmwareHeader)> currentHeader{};
        if (OnReadMemory(GetFirmwareHeaderAddress(), currentHeader) == ResultStatus::ok &&
            std::memcmp(currentHeader.data(), &header, sizeof(FirmwareHeader)) == 0) {
            return ResultStatus::ok;
        }

        return OnWriteMemory(
            GetFirmwareHeaderAddress(),
            std::span<const uint8>(reinterpret_cast<const uint8*>(&header), sizeof(FirmwareHeader))
        );
    }
    


    virtual bool OnCheckBootloaderRequest() const = 0;
    


    virtual uint32 GetMemoryStartAddress() const = 0;
    


    virtual uint32 GetMemorySize() const = 0;
    


    // End memory address (calculated automatically)
    virtual uint32 GetMemoryEndAddress() const {
        return GetMemoryStartAddress() + GetMemorySize();
    }
    


    // Start address of actual application code (header now at end of memory)
    virtual uint32 GetApplicationStartAddress() const {
        return GetMemoryStartAddress();
    }
    
    // Address where firmware header is located (at end of memory)
    virtual uint32 GetFirmwareHeaderAddress() const {
        return GetMemoryStartAddress() + GetMemorySize() - sizeof(FirmwareHeader);
    }
    
    

    // Read-out protection (RDP). Gates Read and UnlockDevice only
    virtual bool IsMemoryProtected() const { return false; }



    // false refuses Read (answered Protected), e.g. when the flash content is encrypted
    virtual bool IsReadBackAllowed() const { return true; }



    // Required alignment of the write address and of every write but the last one
    virtual uint32 GetWriteAlignment() const { return 1; }



    // Called when the write stream is restarted (Erase, ResetWritePos); inheritors restore their decryption state here
    virtual void OnStreamReset() { }
    


    // Memory range validation
    bool IsValidMemoryRange(uint32 address, uint32 size) const {
        if (size == 0) return false;
        if (address < GetMemoryStartAddress()) return false;
        if (address + size > GetMemoryEndAddress()) return false;
        if (address + size < address) return false; // Overflow check
        return true;
    }
    


    virtual bool OnHasEnoughData(std::span<const uint8> buffer) const {
        return buffer.size() > 0;
    }
    


    virtual size_t OnGetRequiredBytes(std::span<const uint8> buffer) const {
        return buffer.size();
    }
    


    virtual std::span<uint8> OnEncryptData(std::span<uint8> data) {
        return data;  // Default without encryption
    }
    


    virtual std::span<uint8> OnDecryptData(std::span<uint8> data) {
        return data;  // Default without encryption
    }
    


    virtual ResultStatus OnWriteMemory(uint32 address, std::span<const uint8> data) = 0;
    


    virtual ResultStatus OnReadMemory(uint32 address, std::span<uint8> data) = 0;
    


    virtual ResultStatus OnEraseIteration(uint32 address, uint32 endAddress, uint32 iteration, uint32& bytesErased) = 0;


    
    // Hook for device unlocking (RDP removal)
    // password - password to check unlock rights
    virtual ResultStatus OnUnlockDevice(std::span<const uint8> /*password*/) {
        // Not supported by default
        return ResultStatus::notSupported;
    }
    


    // Process received data
    // An empty span tells the bootloader that the transport lost framing (reconnect).
    // Only partial input and the pending response are dropped; an erase, the write stream and the state are kept
    void OnDataReceived(std::span<const uint8> data) {
        if (data.empty()) {
            receiveBuffer.Clear();
            partialPacketPending = false;
            hasPendingResponse = false;
            pendingReadAdvance = 0;
            return;
        }

        for (uint8 byte : data) {
            receiveBuffer.Push(byte);
        }
    }
    


    void ProcessReceivedPackets() {
        while (!hasPendingResponse && TryProcessPacket()) {
        }
    }
    


    bool TryProcessPacket() {
        // Drop bytes until the buffer starts with the packet header
        while (receiveBuffer.Size() > 0) {
            Result<uint8> peek = receiveBuffer.Peek();
            if (peek.IsOk() && peek.Value() == 0xAA) {
                break;
            }
            receiveBuffer.Pop();
            partialPacketPending = false;
        }

        if (receiveBuffer.Size() == 0) {
            return false;
        }
        
        // Header, sequence, command and length are needed before anything else can be decided
        if (receiveBuffer.Size() < 4) {
            return DropStalledPacket();
        }
        
        // A length above the packet buffer can never be valid: drop only the header byte and resync on the next 0xAA
        if (static_cast<size_t>(receiveBuffer[3]) > PacketDataMaxSize) {
            uint8 sequence = receiveBuffer[1];
            receiveBuffer.Pop();
            partialPacketPending = false;
            SendError(sequence, BootloaderStatus::InvalidParameters);
            return true;
        }

        CommandPacket packet;
        if (!ReadPacket(packet)) {
            return DropStalledPacket();
        }

        partialPacketPending = false;
        ProcessCommand(packet);
        return true;
    }


    // Called for a header that is not followed by a complete packet yet.
    // The packet is timed from the moment it first reached the front of the buffer, not from the last received byte,
    // so steady traffic behind a stale header cannot keep it alive. Once it stalled longer than packetTimeoutMs,
    // its header byte is dropped so the parser resynchronises
    bool DropStalledPacket() {
        uint32 now = static_cast<uint32>(System::GetMs());
        if (!partialPacketPending) {
            partialPacketPending = true;
            partialPacketSinceMs = now;
            return false;
        }

        if (packetTimeoutMs == 0) {
            return false;
        }
        
        if (static_cast<uint32>(now - partialPacketSinceMs) <= packetTimeoutMs) {
            return false;
        }
        
        receiveBuffer.Pop();
        partialPacketPending = false;
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
        
        // Never copy more than the packet buffer holds (TryProcessPacket already drops such headers)
        if (packet.length > PacketDataMaxSize) {
            return false;
        }

        size_t totalSize = 4 + packet.length; // header + seq + cmd + len + data
        if (receiveBuffer.Size() < totalSize) {
            return false;
        }
        
        // Extract entire packet
        for (size_t i = 0; i < totalSize; i++) {
            Result<uint8> result = receiveBuffer.Pop();
            if (result.IsOk()) {
                reinterpret_cast<uint8*>(&packet)[i] = result.Value();
            } else {
                return false;
            }
        }
        
        return true;
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
                HandleFinalize(packet);
                break;
                
            case Command::ResetWritePos:
                HandleResetWritePos();
                break;
                
            case Command::ResetReadPos:
                HandleResetReadPos();
                break;
                
            case Command::GetHash:
                HandleGetHash();
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
            uint64 bootloaderVersion;
            uint32 maxPacketSize;
            uint32 memoryStart;
            uint32 memorySize;
            uint8 protectionLevel;  // RDP level: 0 = Level0, 1 = Level1, 2 = Level2, 255 = Unknown
            uint8 accessDenied;     // Access status: 0 = allowed, 1 = denied
            uint8 reserved[2];      // Alignment
        } info;
        
        info.bootloaderVersion = bootloaderVersionValue;
        info.maxPacketSize = communication.GetMaxPacketSize();
        info.memoryStart = GetMemoryStartAddress();
        info.memorySize = GetMemorySize();
        
        // Check access level and protection
        auto accessStatus = OnCheckAccessLevel();
        info.accessDenied = (accessStatus == ResultStatus::accessError) ? 1 : 0;
        
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
    


    // Erase, Write and Finalize change the image. Answers the refusal and returns true when the access level forbids
    // flash changes, or when read-out protection is active and allowUpdatesWhenReadProtected is not set.
    bool RefuseImageChange() {
        if (OnCheckAccessLevel() == ResultStatus::accessError) {
            SendError(currentSequence, BootloaderStatus::AccessDenied);
            return true;
        }

        if (!allowUpdatesWhenReadProtected && IsMemoryProtected()) {
            SendError(currentSequence, BootloaderStatus::Protected);
            return true;
        }

        return false;
    }



    void HandleErase(const CommandPacket& packet) {
        if (state == State::Processing || asyncOp.type != AsyncOperation::None) {
            SendError(currentSequence, BootloaderStatus::Busy);
            return;
        }
        
        if (RefuseImageChange()) {
            return;
        }

        // No parameters needed - always erase entire firmware area
        uint32 address = GetMemoryStartAddress();
        uint32 size = GetMemorySize();
        
        // Start asynchronous erase
        asyncOp.type = AsyncOperation::Erasing;
        asyncOp.address = address;
        asyncOp.targetAddress = address + size;
        asyncOp.size = size;
        asyncOp.processed = 0;
        asyncOp.iteration = 0;
        asyncOp.sequence = packet.sequence;
        
        // A new image starts: reset the write stream and the read position
        ResetWriteStream();
        readPosition = 0;
        
        state = State::Processing;
        
        // Immediately return acknowledgment that command is accepted
        // Final response will be sent after operation completes
    }
    


    void HandleWrite(const CommandPacket& packet) {
        if (state == State::Processing) {
            SendError(currentSequence, BootloaderStatus::Busy);
            return;
        }
        
        if (RefuseImageChange()) {
            return;
        }

        // Simple format: just data (no address/offset parameters)
        size_t dataSize = packet.length;
        
        // Check for data
        if (dataSize == 0) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        // Retry of the last accepted Write: answer the same status, the data is not touched again
        if (hasLastWriteSequence && packet.sequence == lastWriteSequence) {
            SendError(currentSequence, lastWriteStatus);
            return;
        }

        // After a failed Write every Write answers the same error until Erase or ResetWritePos
        if (writeStreamFailed) {
            SendError(currentSequence, writeStreamError);
            return;
        }

        // Check that we won't overflow the buffer
        if (accumulatedSize + dataSize > accumulationBuffer.size()) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        // Copy new data to accumulator
        std::copy(
            packet.data.begin(),
            packet.data.begin() + dataSize,
            accumulationBuffer.begin() + accumulatedSize
        );
        accumulatedSize += dataSize;
            
        // Update SHA256 hash with raw data (before decryption)
        dataHasher.Update(std::span<const uint8>(packet.data.data(), dataSize));
        bytesReceived += static_cast<uint32>(dataSize);
        
        // Process accumulated data
        state = State::Processing;
        ProcessResult result = ProcessAccumulatedData();
        state = State::Idle;
        
        BootloaderStatus answer = BootloaderStatus::Success;
        if (result != ProcessResult::Success) {
            writeStreamFailed = true;
            accumulatedSize = 0;
            writeStreamError = (result == ProcessResult::CryptoError) ? BootloaderStatus::CryptoError : BootloaderStatus::Error;
            answer = writeStreamError;
        }

        // Remember the answer so a retry of this sequence gets the same one
        hasLastWriteSequence = true;
        lastWriteSequence = packet.sequence;
        lastWriteStatus = answer;
        SendResponse(answer);
    }
    


    void HandleRead(const CommandPacket& packet) {
        if (state == State::Processing) {
            SendError(currentSequence, BootloaderStatus::Busy);
            return;
        }
        
        // Read-back can be disabled by the inheritor (e.g. encrypted flash)
        if (!IsReadBackAllowed()) {
            SendError(currentSequence, BootloaderStatus::Protected);
            return;
        }

        // Read-out protection (RDP)
        if (IsMemoryProtected()) {
            SendError(currentSequence, BootloaderStatus::Protected);
            return;
        }
        
        // Offset form: 4-byte little-endian offset, retry-safe, does not move the streaming position
        if (packet.length == sizeof(uint32)) {
            uint32 offset = 0;
            std::memcpy(&offset, packet.data.data(), sizeof(uint32));

            if (bytesWritten == 0 || offset >= bytesWritten) {
                SendResponse(BootloaderStatus::EndOfData);
                return;
            }

            uint32 sizeAtOffset = std::min(bytesWritten - offset, static_cast<uint32>(PacketDataMaxSize));
            SendFlashData(offset, sizeAtOffset);
            return;
        }

        // Streaming form (no data) is the only other valid length
        if (packet.length != 0) {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }
        
        // Check if we have data to read
        if (bytesWritten == 0) {
            SendResponse(BootloaderStatus::EndOfData);
            return;
        }
        
        // Check if read position is beyond written data
        if (readPosition >= bytesWritten) {
            SendResponse(BootloaderStatus::EndOfData);
            return;
        }
        
        // Calculate how much data we can read
        uint32 remainingData = bytesWritten - readPosition;
        uint32 sizeToRead = std::min(remainingData, static_cast<uint32>(PacketDataMaxSize));

        // The position moves only when the response was accepted; a queued one moves it when it is really sent
        if (SendFlashData(readPosition, sizeToRead)) {
            if (hasPendingResponse) {
                pendingReadAdvance = sizeToRead;
            } else {
                readPosition += sizeToRead;
            }
        }
    }


    // Sends sizeToRead bytes of the image starting at stream offset 'offset' as a Success response, or an error.
    // Returns true when the response was sent or queued
    bool SendFlashData(uint32 offset, uint32 sizeToRead) {
        uint32 address = GetApplicationStartAddress() + offset;
        bool sent = false;
        
        state = State::Processing;
        
        std::array<uint8, PacketDataMaxSize> readData{};
        std::span<uint8> dataSpan(readData.data(), sizeToRead);
        
        // Read decrypted data directly from Flash (no additional crypto needed)
        ResultStatus status = OnReadMemory(address, dataSpan);
        if (status == ResultStatus::ok) {
            // Data in Flash is already decrypted, encrypt for transmission
            std::span<uint8> encryptedData = OnEncryptData(dataSpan);
            
            // Check that encryption was successful
            if (encryptedData.empty()) {
                SendError(currentSequence, BootloaderStatus::CryptoError);
            }
            // Check that encrypted data will fit in response
            else if (encryptedData.size() <= PacketDataMaxSize) {
                sent = SendResponse(BootloaderStatus::Success, encryptedData);
            } else {
                SendError(currentSequence, BootloaderStatus::Error);
            }
        } else {
            SendError(currentSequence, BootloaderStatus::Error);
        }
        
        state = State::Idle;
        return sent;
    }
    


    void HandleVerify() {
        if (CheckFirmware()) {
            SendResponse(BootloaderStatus::Success);
        } else {
            SendError(currentSequence, BootloaderStatus::Error);
        }
    }
    


    void HandleFinalize(const CommandPacket& packet) {
        if (RefuseImageChange()) {
            return;
        }

        // A failed write stream is never finalized
        if (writeStreamFailed) {
            SendError(currentSequence, writeStreamError);
            return;
        }

        // An incomplete cipher block is still waiting for data: the image is truncated
        if (accumulatedSize != 0) {
            SendError(currentSequence, BootloaderStatus::Error);
            return;
        }

        if (packet.length == sizeof(FirmwareHeader)) {
            // Expected FirmwareHeader from the host: size and CRC32 of the plaintext image, little-endian
            FirmwareHeader expected{};
            std::memcpy(&expected, packet.data.data(), sizeof(FirmwareHeader));

            if (expected.size != bytesWritten) {
                SendError(currentSequence, BootloaderStatus::ChecksumMismatch);
                return;
            }

            std::optional<uint32> actualCrc = CalculateFirmwareCRC32(bytesWritten);
            if (!actualCrc.has_value() || *actualCrc != expected.crc32) {
                SendError(currentSequence, BootloaderStatus::ChecksumMismatch);
                return;
            }
        } else if (packet.length == 0) {
            // Host sent no checksum: allowed only for legacy hosts
            if (requireFinalizeChecksum) {
                SendError(currentSequence, BootloaderStatus::InvalidParameters);
                return;
            }
        } else {
            SendError(currentSequence, BootloaderStatus::InvalidParameters);
            return;
        }

        // Finalize firmware
        ResultStatus status = OnFinalizeFirmware();
        if (status == ResultStatus::ok) {
            SendResponse(BootloaderStatus::Success);
        } else {
            SendError(currentSequence, BootloaderStatus::Error);
        }
    }
    


    void HandleResetWritePos() {
        ResetWriteStream();
        SendResponse(BootloaderStatus::Success);
    }
    


    void HandleResetReadPos() {
        readPosition = 0;
        SendResponse(BootloaderStatus::Success);
    }
    
    
    
    void HandleGetHash() {
        // Get current SHA256 hash of all raw data sent so far
        // Note: This creates a copy of hasher to get current hash without finalizing the original
        SHA256 hasherCopy = dataHasher;  // Copy current state
        auto hash = hasherCopy.Finalize();  // Finalize the copy to get hash
        
        // Send 32-byte SHA256 hash as response
        SendResponse(BootloaderStatus::Success, std::span<const uint8>(hash.data(), hash.size()));
    }
    


    void HandleStart() {
        // Jumping while an erase runs would leave the flash half erased
        if (state == State::Processing || asyncOp.type != AsyncOperation::None) {
            SendError(currentSequence, BootloaderStatus::Busy);
            return;
        }

        if (checkFirmware && !CheckFirmware()) {
            SendError(currentSequence, BootloaderStatus::Error);
            return;
        }

        // Answer first, jump in Process() once the response had time to leave
        SendResponse(BootloaderStatus::Success);
        startTime = System::GetMs() + responseFlushDelayMs;
    }
    


    void HandleReset() {
        SendResponse(BootloaderStatus::Success);

        // Reset after ms
        resetTime = System::GetMs() + responseFlushDelayMs;
    }
    


    void HandleGetStatus() {
        // Wire layout: state, 3 reserved bytes (always zero), bytesReceived
        struct StatusInfo {
            uint8 state;
            uint8 reserved[3];
            uint32 bytesReceived;
        } _APacked;
        
        StatusInfo status{};
        status.state = static_cast<uint8>(state);
        status.bytesReceived = bytesReceived;
        
        SendResponse(
            BootloaderStatus::Success,
            std::span<const uint8>(reinterpret_cast<const uint8*>(&status), sizeof(status))
        );
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
        
        if (status == ResultStatus::ok) {
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
        } else if (result.size() > PacketDataMaxSize) {
            // Response too large
            SendError(currentSequence, BootloaderStatus::Error);
        } else {
            // Success - send data back (empty for write operations is OK)
            SendResponse(BootloaderStatus::Success, result);
        }
    }
    


    // Returns true when the response was sent or queued, false when it was dropped
    bool SendResponse(BootloaderStatus status, std::span<const uint8> data = {}) {
        // One pending response at a time; the flow in Process() makes this unreachable
        if (hasPendingResponse || data.size() > PacketDataMaxSize) {
            return false;
        }

        ResponsePacket response{};
        response.sequence = currentSequence;
        response.status = status;
        response.length = static_cast<uint8>(data.size());
        
        if (!data.empty()) {
            std::copy(data.begin(), data.end(), response.data.begin());
        }
        
        size_t packetSize = offsetof(ResponsePacket, data) + response.length;
        ResultStatus sendStatus = communication.SendData(std::span<const uint8>(
            reinterpret_cast<const uint8*>(&response),
            packetSize
        ));

        if (sendStatus == ResultStatus::ok) {
            return true;
        }

        // Transport busy: keep the response and send it from Process()
        if (sendStatus == ResultStatus::busy) {
            pendingResponse = response;
            pendingResponseSize = packetSize;
            hasPendingResponse = true;
            return true;
        }

        return false;
    }


    // Sends the pending response once the transport is ready. A busy transport keeps it, any other result drops it
    void FlushPendingResponse() {
        if (!hasPendingResponse || !communication.IsReadyToSend()) {
            return;
        }

        ResultStatus sendStatus = communication.SendData(std::span<const uint8>(
            reinterpret_cast<const uint8*>(&pendingResponse),
            pendingResponseSize
        ));

        if (sendStatus == ResultStatus::busy) {
            return;
        }

        // Sent: a queued Read moves the position now. Dropped (other error): the host asks again
        if (sendStatus == ResultStatus::ok) {
            readPosition += pendingReadAdvance;
        }

        hasPendingResponse = false;
        pendingReadAdvance = 0;
    }
    


    void SendError(uint8 sequence, BootloaderStatus status) {
        currentSequence = sequence;
        SendResponse(status);
    }
    


    // CRC32 of the first 'size' bytes of the firmware in flash.
    // Returns std::nullopt on a size or read error; CRC value 0 is a valid result
    std::optional<uint32> CalculateFirmwareCRC32(uint32 size) const {
        if (size == 0) {
            return std::nullopt;
        }
        
        // PROTECTION: check that size doesn't exceed available memory
        uint32 maxFirmwareSize = GetMemorySize() - sizeof(FirmwareHeader);
        if (size > maxFirmwareSize) {
            return std::nullopt;
        }
        
        // Read firmware data in chunks and calculate CRC32
        const uint32 CHUNK_SIZE = 256;  // Read 256 bytes at a time
        std::array<uint8, CHUNK_SIZE> buffer;
        uint32 currentCrc = 0;
        uint32 firmwareStart = GetApplicationStartAddress();
        
        for (uint32 offset = 0; offset < size; offset += CHUNK_SIZE) {
            uint32 chunkSize = std::min(CHUNK_SIZE, size - offset);
            std::span<uint8> chunkSpan(buffer.data(), chunkSize);
            
            // PROTECTION: check read boundaries before calling OnReadMemory
            uint32 readAddress = firmwareStart + offset;
            if (!IsValidMemoryRange(readAddress, chunkSize)) {
                return std::nullopt;  // Attempted read beyond memory boundaries
            }
            
            ResultStatus readStatus = const_cast<IBootloader*>(this)->OnReadMemory(
                readAddress, chunkSpan
            );
            
            if (readStatus != ResultStatus::ok) {
                return std::nullopt;  // Read error
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



    // Restarts the write stream (Erase, ResetWritePos). Inheritors restore their decryption state in OnStreamReset()
    void ResetWriteStream() {
        writePosition = 0;
        bytesWritten = 0;
        bytesReceived = 0;
        accumulatedSize = 0;
        dataHasher.Reset();
        hasLastWriteSequence = false;
        writeStreamFailed = false;
        writeStreamError = BootloaderStatus::Error;
        OnStreamReset();
    }

        
    // Process accumulated data: decrypt and write every complete block, keep the unused tail
    ProcessResult ProcessAccumulatedData() {
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
                return ProcessResult::Error;
            }
            
            // Prepare data for decryption
            std::span<uint8> dataToDecrypt(
                accumulationBuffer.data() + processedBytes,
                bytesToProcess
            );
            
            // Decrypt (may return different size)
            std::span<uint8> decryptedData = OnDecryptData(dataToDecrypt);
            
            // Check that decryption was successful
            if (decryptedData.empty()) {
                // Decryption error - this is critical error
                return ProcessResult::CryptoError;
            }
            
            // Calculate write address from current stream position
            uint32 writeAddress = GetApplicationStartAddress() + writePosition;
            
            // Check address validity for writing
            if (!IsValidMemoryRange(writeAddress, decryptedData.size())) {
                return ProcessResult::Error;
            }
            
            // Every write must start aligned: only the final chunk of an image may be unaligned
            uint32 alignment = std::max<uint32>(GetWriteAlignment(), 1);
            if (writePosition % alignment != 0) {
                return ProcessResult::Error;
            }
            
            // The firmware header at the end of the region is reserved (uint64 cannot overflow here)
            uint64 writeEnd = static_cast<uint64>(writeAddress) + decryptedData.size();
            if (writeEnd > GetFirmwareHeaderAddress()) {
                return ProcessResult::Error;
            }

            ResultStatus status = OnWriteMemory(writeAddress, decryptedData);
            if (status != ResultStatus::ok) {
                return ProcessResult::Error;
            }

            bytesWritten += static_cast<uint32>(decryptedData.size());  // Real data (after decryption)
            writePosition += static_cast<uint32>(decryptedData.size());  // Update position for next write
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
        
        return ProcessResult::Success;
    }



    void ProcessAsyncOperation() {
        if (asyncOp.type == AsyncOperation::None) {
            return;
        }
        
        switch (asyncOp.type) {
            case AsyncOperation::Erasing:
                ProcessAsyncErase();
                break;
                
            default:
                break;
        }
    }



    void ProcessAsyncErase() {
        // One erase step: ok = finished, inProgress = call again, anything else = failed
        uint32 bytesErased = 0;
        ResultStatus status = OnEraseIteration(
        	asyncOp.address + asyncOp.processed,
			asyncOp.targetAddress,
			asyncOp.iteration,
			bytesErased
		);
        
        if (status == ResultStatus::ok) {
            // Erase completed
            asyncOp.type = AsyncOperation::None;
            state = State::Idle;
            currentSequence = asyncOp.sequence;
            SendResponse(BootloaderStatus::Success);
            return;
        }

        if (status != ResultStatus::inProgress) {
            // Any other status stops the erase
            asyncOp.type = AsyncOperation::None;
            state = State::Idle;
            SendError(asyncOp.sequence, BootloaderStatus::Error);
            return;
        }
        
        // Still in progress: continue in the next Process()
        asyncOp.processed += bytesErased;
        asyncOp.iteration++;
    }
};
