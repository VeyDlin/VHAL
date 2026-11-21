#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// Constants and descriptor helpers of the CDC-ACM function: the Communications Device Class (class 0x02, subclass 0x02 for the
// abstract control model, the data class 0x0A) and the PSTN devices specification 1.2 for the requests and the notification. The
// core's USBClassCode lists the class 0x02 and 0x0A is kept here, next to the other class constants.
constexpr uint8 USBCdcDataClassCode = 0x0A;
constexpr uint8 USBCdcAcmSubClass = 0x02;
constexpr uint8 USBCdcNoSubClass = 0x00;
constexpr uint8 USBCdcNoProtocol = 0x00;
constexpr uint8 USBCdcClassSpecificInterface = 0x24;
constexpr uint8 USBCdcHeaderSubtype = 0x00;
constexpr uint8 USBCdcCallManagementSubtype = 0x01;
constexpr uint8 USBCdcAcmSubtype = 0x02;
constexpr uint8 USBCdcUnionSubtype = 0x06;
// bcdCDC in the header descriptor. See USBCdcAcmBuilder for the value the builder writes.
constexpr uint16 USBCdcSpecificationRelease = 0x0110;
// bmCapabilities of the ACM descriptor (PSTN 1.2 section 6.3.2): bit 1 is set, so the device supports Set_Line_Coding,
// Set_Control_Line_State, Get_Line_Coding and the Serial_State notification. The other bits are not supported.
constexpr uint8 USBCdcAcmCapabilities = 0x02;
// bmCapabilities of the Call Management descriptor: 0, the device does not handle call management itself.
constexpr uint8 USBCdcCallManagementCapabilities = 0x00;


// Class requests of the communication interface (PSTN 1.2 table 19). The values are the bRequest codes.
constexpr uint8 USBCdcSetLineCoding = 0x20;
constexpr uint8 USBCdcGetLineCoding = 0x21;
constexpr uint8 USBCdcSetControlLineState = 0x22;
constexpr uint8 USBCdcSendBreak = 0x23;
// The notification code of SERIAL_STATE (PSTN 1.2 table 21).
constexpr uint8 USBCdcSerialStateNotification = 0x20;
// The notification header is bmRequestType 0xA1 (device to host, class, interface), then bNotification and wValue 0.
constexpr uint8 USBCdcNotificationRequestType = 0xA1;
// wValue of SET_CONTROL_LINE_STATE: DTR is bit 0 ("terminal open"), RTS is bit 1 (PSTN 1.2 table 17).
constexpr uint16 USBCdcControlDtr = 0x0001;
constexpr uint16 USBCdcControlRts = 0x0002;
// The UART state bitmap of SERIAL_STATE (PSTN 1.2 table 31). Bits 7 to 15 are reserved.
constexpr uint16 USBCdcUartRxCarrier = 0x0001;
constexpr uint16 USBCdcUartTxCarrier = 0x0002;
constexpr uint16 USBCdcUartBreak = 0x0004;
constexpr uint16 USBCdcUartRingSignal = 0x0008;
constexpr uint16 USBCdcUartFraming = 0x0010;
constexpr uint16 USBCdcUartParity = 0x0020;
constexpr uint16 USBCdcUartOverRun = 0x0040;
// The notification length: the 8-byte request header and the 2-byte UART state bitmap.
constexpr uint16 USBCdcSerialStateSize = 10;
// The line coding block of SET_LINE_CODING and GET_LINE_CODING (PSTN 1.2 section 6.3.11): 7 bytes.
constexpr std::size_t USBCdcLineCodingSize = 7;


// The line coding of a serial port. The defaults are the ones a host expects before it sets anything: 115200 baud, one stop bit,
// no parity, 8 data bits.
struct USBCdcLineCoding {
	uint32 dteRate = 115200;
	// bCharFormat: 0 is one stop bit, 1 one and a half, 2 two.
	uint8 stopBits = 0;
	// bParityType: 0 none, 1 odd, 2 even, 3 mark, 4 space.
	uint8 parity = 0;
	// bDataBits: 5, 6, 7, 8 or 16.
	uint8 dataBits = 8;
};


// Encodes the 7-byte line coding block: dwDTERate little-endian, then the three one-byte fields.
constexpr std::array<uint8, USBCdcLineCodingSize> USBCdcEncodeLineCoding(const USBCdcLineCoding& coding) {
	std::array<uint8, USBCdcLineCodingSize> bytes{};
	bytes[0] = static_cast<uint8>(coding.dteRate & 0xFF);
	bytes[1] = static_cast<uint8>((coding.dteRate >> 8) & 0xFF);
	bytes[2] = static_cast<uint8>((coding.dteRate >> 16) & 0xFF);
	bytes[3] = static_cast<uint8>((coding.dteRate >> 24) & 0xFF);
	bytes[4] = coding.stopBits;
	bytes[5] = coding.parity;
	bytes[6] = coding.dataBits;
	return bytes;
}


// Decodes the 7-byte line coding block that SET_LINE_CODING carries.
constexpr USBCdcLineCoding USBCdcDecodeLineCoding(std::span<const uint8> bytes) {
	USBCdcLineCoding coding;
	coding.dteRate = static_cast<uint32>(bytes[0]) | (static_cast<uint32>(bytes[1]) << 8) | (static_cast<uint32>(bytes[2]) << 16) | (static_cast<uint32>(bytes[3]) << 24);
	coding.stopBits = bytes[4];
	coding.parity = bytes[5];
	coding.dataBits = bytes[6];
	return coding;
}


// The class-specific functional descriptors (CDC 1.2 section 5.2.3). Each is written with its bLength first, then the CS_INTERFACE
// type 0x24 and its subtype. The interface numbers are the values the configuration resolved.
//
// Header: bcdCDC, the release the functional descriptors follow.
constexpr std::array<uint8, 5> MakeUSBCdcHeaderDescriptor() {
	std::array<uint8, 5> bytes{};
	bytes[0] = 5;
	bytes[1] = USBCdcClassSpecificInterface;
	bytes[2] = USBCdcHeaderSubtype;
	USBDescriptorPut16(bytes, 3, USBCdcSpecificationRelease);
	return bytes;
}


// Call Management: bmCapabilities and the data interface that carries the calls.
constexpr std::array<uint8, 5> MakeUSBCdcCallManagementDescriptor(uint8 dataInterface) {
	std::array<uint8, 5> bytes{};
	bytes[0] = 5;
	bytes[1] = USBCdcClassSpecificInterface;
	bytes[2] = USBCdcCallManagementSubtype;
	bytes[3] = USBCdcCallManagementCapabilities;
	bytes[4] = dataInterface;
	return bytes;
}


// Abstract Control Management: bmCapabilities only.
constexpr std::array<uint8, 4> MakeUSBCdcAcmDescriptor() {
	std::array<uint8, 4> bytes{};
	bytes[0] = 4;
	bytes[1] = USBCdcClassSpecificInterface;
	bytes[2] = USBCdcAcmSubtype;
	bytes[3] = USBCdcAcmCapabilities;
	return bytes;
}


// Union with one slave: the master (the communication interface) and the data interface.
constexpr std::array<uint8, 5> MakeUSBCdcUnionDescriptor(uint8 masterInterface, uint8 slaveInterface) {
	std::array<uint8, 5> bytes{};
	bytes[0] = 5;
	bytes[1] = USBCdcClassSpecificInterface;
	bytes[2] = USBCdcUnionSubtype;
	bytes[3] = masterInterface;
	bytes[4] = slaveInterface;
	return bytes;
}


// The SERIAL_STATE notification (PSTN 1.2 section 6.3.5): the 8-byte request header with wIndex set to the communication interface
// and wLength 2, then the UART state bitmap. The host reads it on the interrupt IN endpoint.
constexpr std::array<uint8, USBCdcSerialStateSize> MakeUSBCdcSerialStateNotification(uint8 communicationInterface, uint16 uartState) {
	std::array<uint8, USBCdcSerialStateSize> bytes{};
	bytes[0] = USBCdcNotificationRequestType;
	bytes[1] = USBCdcSerialStateNotification;
	bytes[2] = 0;
	bytes[3] = 0;
	bytes[4] = communicationInterface;
	bytes[5] = 0;
	bytes[6] = 2;
	bytes[7] = 0;
	bytes[8] = static_cast<uint8>(uartState & 0xFF);
	bytes[9] = static_cast<uint8>(uartState >> 8);
	return bytes;
}


// Constants of the CDC-NCM function (USB CDC Subclass Specification for Network Control Model Devices 1.0, with CDC 1.2 and ECM 1.2).
// The communication interface is class 0x02, subclass 0x0D, protocol 0x00. The data interface is class 0x0A with protocol 0x01, which
// is NTB framing (network transfer blocks, see NcmNtb16.h).
constexpr uint8 USBCdcNcmSubClass = 0x0D;
constexpr uint8 USBCdcNtbProtocol = 0x01;
// Functional descriptor subtypes: Ethernet Networking (ECM 1.2 section 3.2.1) and NCM (NCM 1.0 section 5.2.1).
constexpr uint8 USBCdcEthernetSubtype = 0x0F;
constexpr uint8 USBCdcNcmSubtype = 0x1A;
// The lengths of the two functional descriptors, and bcdNcmVersion 1.00.
constexpr uint8 USBCdcEthernetDescriptorLength = 13;
constexpr uint8 USBCdcNcmDescriptorLength = 6;
constexpr uint16 USBCdcNcmVersion = 0x0100;
// wMaxSegmentSize of the Ethernet descriptor: the largest frame, a 1500-byte payload with its 14-byte header.
constexpr uint16 USBCdcMaxSegmentSize = 1514;
// bmNetworkCapabilities bits (NCM 1.0 section 5.2.1). The function sets only the bits it implements.
constexpr uint8 USBCdcNcmCapPacketFilter = 0x01;
constexpr uint8 USBCdcNcmCapNetAddress = 0x02;
constexpr uint8 USBCdcNcmCapEncapsulatedCommand = 0x04;
constexpr uint8 USBCdcNcmCapMaxDatagramSize = 0x08;
constexpr uint8 USBCdcNcmCapCrcMode = 0x10;
constexpr uint8 USBCdcNcmCapNtbInputSize = 0x20;
// Class requests of the communication interface (NCM 1.0 table 6-2, ECM 1.2 table 6). The values are the bRequest codes.
constexpr uint8 USBCdcSetEthernetMulticastFilters = 0x40;
constexpr uint8 USBCdcSetEthernetPacketFilter = 0x43;
constexpr uint8 USBCdcGetEthernetStatistic = 0x44;
constexpr uint8 USBCdcGetNtbParameters = 0x80;
constexpr uint8 USBCdcGetNetAddress = 0x81;
constexpr uint8 USBCdcSetNetAddress = 0x82;
constexpr uint8 USBCdcGetNtbFormat = 0x83;
constexpr uint8 USBCdcSetNtbFormat = 0x84;
constexpr uint8 USBCdcGetNtbInputSize = 0x85;
constexpr uint8 USBCdcSetNtbInputSize = 0x86;
constexpr uint8 USBCdcGetMaxDatagramSize = 0x87;
constexpr uint8 USBCdcSetMaxDatagramSize = 0x88;
constexpr uint8 USBCdcGetCrcMode = 0x89;
constexpr uint8 USBCdcSetCrcMode = 0x8A;
// wValue of SET_NTB_FORMAT: 0 is NTB-16, 1 is NTB-32.
constexpr uint16 USBCdcNtbFormat16 = 0;
constexpr uint16 USBCdcNtbFormat32 = 1;
// The notification codes (NCM 1.0 table 6-3, ECM 1.2 table 4).
constexpr uint8 USBCdcNetworkConnectionNotification = 0x00;
constexpr uint8 USBCdcConnectionSpeedChangeNotification = 0x2A;
// The notification lengths: NETWORK_CONNECTION carries no data, CONNECTION_SPEED_CHANGE two 32-bit rates.
constexpr std::size_t USBCdcNetworkConnectionSize = 8;
constexpr std::size_t USBCdcConnectionSpeedChangeSize = 16;


// The Ethernet Networking functional descriptor (ECM 1.2 section 5.2.1), 13 bytes. iMACAddress is written as 0 here: the device gives
// it through a run-time string (the caller fills in the index with RawWithRuntimeString). The statistics bitmap and the power filter
// count are 0, because the function keeps neither.
constexpr std::array<uint8, USBCdcEthernetDescriptorLength> MakeUSBCdcEthernetDescriptor() {
	std::array<uint8, USBCdcEthernetDescriptorLength> bytes{};
	bytes[0] = USBCdcEthernetDescriptorLength;
	bytes[1] = USBCdcClassSpecificInterface;
	bytes[2] = USBCdcEthernetSubtype;
	USBDescriptorPut16(bytes, 8, USBCdcMaxSegmentSize);
	return bytes;
}


// The NCM functional descriptor (NCM 1.0 section 5.2.1), 6 bytes: bcdNcmVersion and bmNetworkCapabilities.
constexpr std::array<uint8, USBCdcNcmDescriptorLength> MakeUSBCdcNcmDescriptor(uint8 capabilities) {
	std::array<uint8, USBCdcNcmDescriptorLength> bytes{};
	bytes[0] = USBCdcNcmDescriptorLength;
	bytes[1] = USBCdcClassSpecificInterface;
	bytes[2] = USBCdcNcmSubtype;
	USBDescriptorPut16(bytes, 3, USBCdcNcmVersion);
	bytes[5] = capabilities;
	return bytes;
}


// Writes a 32-bit value little-endian at offset.
constexpr void USBCdcPut32(std::span<uint8> bytes, std::size_t offset, uint32 value) {
	bytes[offset] = static_cast<uint8>(value & 0xFF);
	bytes[offset + 1] = static_cast<uint8>((value >> 8) & 0xFF);
	bytes[offset + 2] = static_cast<uint8>((value >> 16) & 0xFF);
	bytes[offset + 3] = static_cast<uint8>((value >> 24) & 0xFF);
}


// NETWORK_CONNECTION (NCM 1.0 section 6.3.1): bmRequestType 0xA1, bNotificationCode 0, wValue 1 when connected and 0 when not, wIndex
// the communication interface, wLength 0. The packet is 8 bytes.
constexpr std::array<uint8, USBCdcNetworkConnectionSize> MakeUSBCdcNetworkConnectionNotification(uint8 communicationInterface, bool connected) {
	std::array<uint8, USBCdcNetworkConnectionSize> bytes{};
	bytes[0] = USBCdcNotificationRequestType;
	bytes[1] = USBCdcNetworkConnectionNotification;
	bytes[2] = connected ? 1 : 0;
	bytes[3] = 0;
	bytes[4] = communicationInterface;
	bytes[5] = 0;
	bytes[6] = 0;
	bytes[7] = 0;
	return bytes;
}


// CONNECTION_SPEED_CHANGE (NCM 1.0 section 6.3.2): the same header with code 0x2A, wLength 8, then DLBitRate and ULBitRate, each a
// 32-bit little-endian rate in bit/s. The packet is 16 bytes.
constexpr std::array<uint8, USBCdcConnectionSpeedChangeSize> MakeUSBCdcConnectionSpeedChangeNotification(
	uint8 communicationInterface,
	uint32 downRate,
	uint32 upRate)
{
	std::array<uint8, USBCdcConnectionSpeedChangeSize> bytes{};
	bytes[0] = USBCdcNotificationRequestType;
	bytes[1] = USBCdcConnectionSpeedChangeNotification;
	bytes[2] = 0;
	bytes[3] = 0;
	bytes[4] = communicationInterface;
	bytes[5] = 0;
	bytes[6] = 8;
	bytes[7] = 0;
	USBCdcPut32(bytes, 8, downRate);
	USBCdcPut32(bytes, 12, upRate);
	return bytes;
}
