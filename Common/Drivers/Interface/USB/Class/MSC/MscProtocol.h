#pragma once
#include <Utilities/DataTypes.h>
#include <cstddef>


// Values of the USB Mass Storage Class (MSC 1.x) and of its Bulk-Only Transport (BOT 1.0): the interface class, the SCSI
// transparent command set, the wrappers, the class requests and the status values. Everything here is the wire value.


// The interface: class 0x08, subclass 0x06 (SCSI transparent command set), protocol 0x50 (Bulk-Only Transport).
constexpr uint8 MscInterfaceClass = 0x08;
constexpr uint8 MscSubClassScsi = 0x06;
constexpr uint8 MscProtocolBulkOnly = 0x50;


// Class requests on the interface (BOT 1.0 sections 3.1 and 3.2).
constexpr uint8 MscRequestBulkOnlyReset = 0xFF;
constexpr uint8 MscRequestGetMaxLun = 0xFE;


// The bulk endpoints are full-speed bulk endpoints with 64-byte packets.
constexpr uint16 MscPacketSize = 64;


// The command block wrapper: signature 0x43425355 ("USBC", little endian), tag, data transfer length, flags, LUN, command length
// and the 16-byte command block. 31 bytes in all.
constexpr uint32 MscCbwSignature = 0x43425355;
constexpr std::size_t MscCbwLength = 31;
constexpr std::size_t MscCbwCommandOffset = 15;
constexpr std::size_t MscCommandLength = 16;


// The command status wrapper: signature 0x53425355 ("USBS"), the tag, the residue and the status. 13 bytes in all.
constexpr uint32 MscCswSignature = 0x53425355;
constexpr std::size_t MscCswLength = 13;


// bCSWStatus values (BOT 1.0 table 5.3).
constexpr uint8 MscStatusPassed = 0x00;
constexpr uint8 MscStatusFailed = 0x01;
constexpr uint8 MscStatusPhaseError = 0x02;


// bmCBWFlags bit 7: 1 is Data-In from the device, 0 is Data-Out to the device.
constexpr uint8 MscFlagDataIn = 0x80;


// SCSI operation codes (SPC-4 and SBC-3) of the commands the function answers.
constexpr uint8 ScsiTestUnitReady = 0x00;
constexpr uint8 ScsiRequestSense = 0x03;
constexpr uint8 ScsiInquiry = 0x12;
constexpr uint8 ScsiModeSense6 = 0x1A;
constexpr uint8 ScsiStartStopUnit = 0x1B;
constexpr uint8 ScsiPreventAllowMediumRemoval = 0x1E;
constexpr uint8 ScsiReadFormatCapacities = 0x23;
constexpr uint8 ScsiReadCapacity10 = 0x25;
constexpr uint8 ScsiRead10 = 0x28;
constexpr uint8 ScsiWrite10 = 0x2A;
constexpr uint8 ScsiVerify10 = 0x2F;
constexpr uint8 ScsiSynchronizeCache10 = 0x35;
constexpr uint8 ScsiModeSense10 = 0x5A;


// Sense keys (SPC-4 table 28) of the errors the function reports.
constexpr uint8 ScsiSenseNoSense = 0x00;
constexpr uint8 ScsiSenseNotReady = 0x02;
constexpr uint8 ScsiSenseMediumError = 0x03;
constexpr uint8 ScsiSenseIllegalRequest = 0x05;
constexpr uint8 ScsiSenseDataProtect = 0x07;


// Sense data of one error: the key with its additional sense code and qualifier.
struct MscSense {
	uint8 key = ScsiSenseNoSense;
	uint8 asc = 0x00;
	uint8 ascq = 0x00;
};


// The errors of the function, as the SPC and SBC tables name them.
constexpr MscSense MscSenseNone{ ScsiSenseNoSense, 0x00, 0x00 };
constexpr MscSense MscSenseInvalidCommand{ ScsiSenseIllegalRequest, 0x20, 0x00 };
constexpr MscSense MscSenseLbaOutOfRange{ ScsiSenseIllegalRequest, 0x21, 0x00 };
constexpr MscSense MscSenseInvalidField{ ScsiSenseIllegalRequest, 0x24, 0x00 };
constexpr MscSense MscSenseMediumRemovalPrevented{ ScsiSenseIllegalRequest, 0x53, 0x02 };
constexpr MscSense MscSenseNotPresent{ ScsiSenseNotReady, 0x3A, 0x00 };
constexpr MscSense MscSenseWriteProtected{ ScsiSenseDataProtect, 0x27, 0x00 };
constexpr MscSense MscSenseUnrecoveredRead{ ScsiSenseMediumError, 0x11, 0x00 };
constexpr MscSense MscSenseWriteError{ ScsiSenseMediumError, 0x0C, 0x00 };
