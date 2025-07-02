#pragma once
#include <Utilities/DataTypes.h>

#define StatusAssert(__e) if(__e != Status::ok) return __e

struct Status {
    typedef enum : uint8 {
        ok,                                         // Operation completed successfully
        error,                                      // General error
        unknownError,                               // Unknown error
        busError,                                   // Bus error
        readError,                                  // Data read error
        writeError,                                 // Data write error
        accessError,                                // Access error
        interfaceError,                             // Interface error
        busy,                                       // Device is busy
        ready,                                      // Device is ready
        run,                                        // Device is running
        stop,                                       // Device is stopped
        timeout,                                    // Timeout exceeded
        notExist,                                   // Object does not exist
        notReady,                                   // Device is not ready
        notRecognized,                              // Object not recognized
        notAvailable,                               // Object not available
        notSupported,                               // Function not supported
        noConnection,                               // No connection
        noMatches,                                  // No matches found
        notFound,                                   // Object not found
        noDetected,                                 // Object not detected
        noInit,                                     // Object not initialized
        reset,                                      // Device has been reset
        powerFailure,                               // Power failure
        invalidParameter,                           // Invalid parameter
        invalidData,                                // Invalid data
        filled,                                     // Fully filled
        crcError,                                   // CRC error
        outOfRange,                                 // Out of range
        overheat,                                   // Overheating
        undercooling,                               // Undercooling
        badAddress,                                 // Invalid address
        ack,                                        // ACK
        nack,                                       // NACK
        wrongHashValue,                             // Incorrect hash value
        on,                                         // Turned on
        off,                                        // Turned off
        noSuchFileOrDirectory,                      // File or directory does not exist
        noSuchProcess,                              // Process does not exist
        ioError,                                    // Input/output error
        noSuchDeviceOrAddress,                      // Device or address does not exist
        argListTooLong,                             // Argument list too long
        badFileNumber,                              // Invalid file number
        tryAgain,                                   // Try again
        outOfMemory,                                // Out of memory
        permissionDenied,                           // Permission denied
        deviceOrResourceBusy,                       // Device or resource busy
        fileExists,                                 // File already exists
        fileNotExists,                              // File does not exist
        noSuchDevice,                               // Device does not exist
        notDirectory,                               // Not a directory
        isDirectory,                                // Is a directory
        invalidArgument,                            // Invalid argument
        fileTableOverflow,                          // File table overflow
        tooManyOpenFiles,                           // Too many open files
        textFileBusy,                               // Text file is busy
        fileTooLarge,                               // File is too large
        noSpaceLeftOnDevice,                        // No space left on device
        readOnly,                                   // Read-only
        tooManyLinks,                               // Too many links
        resourceDeadLock,                           // Resource deadlock
        fileNameTooLong,                            // File name is too long
        functionNotImplemented,                     // Function not implemented
        directoryNotEmpty,                          // Directory is not empty
        identifierRemoved,                          // Identifier removed
        protocolDriverNotAttached,                  // Protocol driver not attached
        invalidExchange,                            // Invalid exchange
        invalidRequestDescriptor,                   // Invalid request descriptor
        invalidRequestCode,                         // Invalid request code
        invalidSlot,                                // Invalid slot
        badFileFormat,                              // Invalid file format
        noDataAvailable,                            // No data available
        timerExpired,                               // Timer expired
        objectIsRemote,                             // Object is remote
        communicationErrorOnSend,                   // Communication error during sending
        protocolError,                              // Protocol error
        valueTooLargeforDefinedDataType,            // Value too large for defined data type
        nameNotUniqueon,                            // Name is not unique
        fileDescriptorInBadState,                   // File descriptor in bad state
        illegalByteSequence,                        // Illegal byte sequence
        streamsPipeError,                           // Streams/pipe error
        tooManyUsers,                               // Too many users
        destinationAddressRequired,                 // Destination address required
        messageTooLong,                             // Message too long
        addressAlreadyinUse,                        // Address already in use
        cannotAssignRequestedAddress,               // Cannot assign requested address
        networkIsDown,                              // Network is down
        networkIsUnreachable,                       // Network is unreachable
        networkDroppedConnection,                   // Network dropped connection
        noBufferSpaceAvailable,                     // No buffer space available
        connectionTimedout,                         // Connection timed out
        retryExhausted,                             // Retry are exhausted
        connectionRefused,                          // Connection refused
        hostIsdown,                                 // Host is down
        operationNowinProgress,                     // Operation is now in progress
        empty,                                      // Empty
        listen,                                     // Listening
		authenticationFailed,                       // Authentication failed
		authorizationFailed,                        // Authorization failed
		partialSuccess,                             // Operation partially succeeded
		dataCorrupted,                              // Data is corrupted or in an invalid format
		operationAborted,                           // Operation was aborted
		networkReset,                               // Network reset occurred
		quotaExceeded,                              // Resource usage quota exceeded
		licenseExpired,                             // License has expired
		diskFull,                                   // Disk is full
		serviceUnavailable,                         // Service is temporarily unavailable
		maintenanceMode,                            // System is in maintenance mode
		rateLimitExceeded,                          // Rate limit for requests exceeded
		checksumMismatch,                           // Checksum does not match, data may be corrupted
		resourceLocked,                             // Resource is locked by another process
		connectionClosed,                           // Connection was closed by the remote side
		hardwareFailure,                            // Hardware failure detected
		normalOperation,                            // System is operating in normal mode
		sleepMode,                                  // Device is in sleep mode to save power
		powerSaving,                                // Power saving mode activated
		calibrationRequired,                        // Calibration required for sensors or other elements
		initializing,                               // Device is initializing components
		calibrationInProgress,                      // Calibration in progress
		selfTestInProgress,                         // Self-testing is in progress
		selfTestPassed,                             // Self-test completed successfully
		selfTestFailed,                             // Self-test failed
		communicationActive,                        // Actively exchanging data with another device
		updateRequired,                             // Firmware or configuration update required
		emergencyStop,                              // Device stopped in emergency mode
		charging,                                   // Battery charging in progress
		charged,                                    // Battery fully charged
		discharging,                                // Battery discharging
		lowBattery,                                 // Low battery level
		standby,                                    // Device is in standby mode
		thermalShutdown,                            // Emergency shutdown due to overheating
		underVoltageLockout,                        // Shutdown due to insufficient supply voltage
		resetPending,                               // Pending reset, waiting for operations to complete
		softwareUpdateRequired,                     // Software update required for normal operation
		securityBreachDetected,                     // Security breach detected
		inProgress,                                 // Process or operation is in progress
		faultRecovery,                              // Recovering from a fault, attempting system restart
		overPressure,                               // Overpressure condition detected (for systems with pressure sensors)
		signalLost,                                 // Signal lost, possible connection issue
		waitForEvent,                               // Waiting for an external event to continue
		checksumError,                              // Checksum error detected
        outOfBounds,                                // Out of array bounds or range
        memoryLeak,                                 // Memory leak
        stackOverflow,                              // Stack overflow
        bufferOverflow,                             // Buffer overflow
        overflow,                                   // Overflow
        heapCorruption,                             // Heap corruption
        overVoltage,                                // Overvoltage
        underVoltage,                               // Undervoltage
        firmwareUpdateRequired,                     // Firmware update required
        sensorFailure,                              // Sensor failure
        userAbort,                                  // User abort
        operationCancelled,                         // Operation cancelled
        deviceUnresponsive,                         // Device unresponsive
        paused,                                     // Execution paused
        terminating,                                // Terminating
        lowSignalStrength,                          // Low signal strength
        clockSyncError,                             // Clock synchronization error
		low,										// Low
		high										// High
    } statusType;

	static inline bool IsError(statusType type) { return type != ok; }
	static inline bool IsOk(statusType type) { return type == ok; }

	struct errorDebugInfo {
		const char *file;
		int line;
	};

	template<class returnType>
	struct info {
		statusType type;
		returnType data;
        info(statusType t, returnType d) : type(t), data(d) {}
        info(returnType d) : type(ok), data(d) {}
        info(statusType t) : type(t), data() {}
        info() : type(), data() {}
		inline bool IsError() { return type != ok; }
		inline bool IsOk() { return type == ok; }
	};

	template<class returnType>
	struct infoDebug {
		statusType type;
		returnType data;
		errorDebugInfo error;
	};
};
