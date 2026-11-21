#pragma once
#include <Adapter/Utilities/USBTypes.h>
#include <System/System.h>

// USB 2.0 chapter 9 values for the device core and the classes. Enumerator values are the wire values. bRequest stays a
// plain uint8 in the setup packet, because class and vendor request codes belong to the application.


enum class USBDirection : uint8 {
	Out = 0,
	In = 1
};


enum class USBRequestType : uint8 {
	Standard = 0,
	Class = 1,
	Vendor = 2,
	Reserved = 3
};


// bmRequestType bits 4..0. Values above Other are reserved and match no recipient, so the core stalls such requests.
enum class USBRecipient : uint8 {
	Device = 0,
	Interface = 1,
	Endpoint = 2,
	Other = 3
};


enum class USBStandardRequest : uint8 {
	GetStatus = 0,
	ClearFeature = 1,
	SetFeature = 3,
	SetAddress = 5,
	GetDescriptor = 6,
	SetDescriptor = 7,
	GetConfiguration = 8,
	SetConfiguration = 9,
	GetInterface = 10,
	SetInterface = 11,
	SynchFrame = 12
};


enum class USBDescriptorType : uint8 {
	Device = 1,
	Configuration = 2,
	String = 3,
	Interface = 4,
	Endpoint = 5,
	DeviceQualifier = 6,
	OtherSpeedConfiguration = 7,
	InterfacePower = 8,
	OTG = 9,
	Debug = 10,
	InterfaceAssociation = 11,
	BOS = 15,
	DeviceCapability = 16
};


// wValue of SET_FEATURE and CLEAR_FEATURE with the recipient selecting which feature it names.
enum class USBFeature : uint16 {
	EndpointHalt = 0,
	DeviceRemoteWakeup = 1,
	TestMode = 2
};


enum class USBDeviceState : uint8 {
	Detached,
	Default,
	Address,
	Configured,
	Suspended
};


// The name of a device state for a status line, as the USB 2.0 chapter 9 names it. A value outside the enum gives "Unknown", so a
// state added later shows as Unknown until its name is written here.
constexpr const char* USBDeviceStateName(USBDeviceState state) {
	switch (state) {
		case USBDeviceState::Detached: {
			return "Detached";
		}

		case USBDeviceState::Default: {
			return "Default";
		}

		case USBDeviceState::Address: {
			return "Address";
		}

		case USBDeviceState::Configured: {
			return "Configured";
		}

		case USBDeviceState::Suspended: {
			return "Suspended";
		}
	}

	return "Unknown";
}


// bDeviceClass and bInterfaceClass values used by the stack.
enum class USBClassCode : uint8 {
	// Device class only: the classes are given per interface.
	PerInterface = 0x00,
	Cdc = 0x02,
	Hid = 0x03,
	MassStorage = 0x08,
	// Device class 0xEF, subclass 0x02, protocol 0x01 marks a device that uses interface association descriptors.
	Miscellaneous = 0xEF,
	// Includes DFU (subclass 0x01).
	ApplicationSpecific = 0xFE,
	Vendor = 0xFF
};


// The SETUP packet decoded into fields. Parse() reads the little-endian wire fields, so the struct holds plain integers.
struct USBSetupPacket {
	USBDirection direction = USBDirection::Out;
	USBRequestType type = USBRequestType::Standard;
	USBRecipient recipient = USBRecipient::Device;
	// bRequest. A USBStandardRequest value when type is Standard.
	uint8 request = 0;
	uint16 value = 0;
	uint16 index = 0;
	// wLength: the number of bytes of the data stage the host expects. Zero means no data stage.
	uint16 length = 0;


	static constexpr USBSetupPacket Parse(const USBSetupBytes& bytes) noexcept {
		USBSetupPacket packet;
		uint8 requestType = bytes.bytes[0];
		packet.direction = (requestType & 0x80) != 0 ? USBDirection::In : USBDirection::Out;
		packet.type = static_cast<USBRequestType>((requestType >> 5) & 0x03);
		packet.recipient = static_cast<USBRecipient>(requestType & 0x1F);
		packet.request = bytes.bytes[1];
		packet.value = static_cast<uint16>(bytes.bytes[2] | (bytes.bytes[3] << 8));
		packet.index = static_cast<uint16>(bytes.bytes[4] | (bytes.bytes[5] << 8));
		packet.length = static_cast<uint16>(bytes.bytes[6] | (bytes.bytes[7] << 8));
		return packet;
	}


	// The inverse of Parse(). The host tests build their packets with it.
	constexpr USBSetupBytes ToBytes() const noexcept {
		USBSetupBytes bytes;
		uint8 directionBit = direction == USBDirection::In ? 0x80 : 0x00;
		bytes.bytes[0] = static_cast<uint8>(directionBit | (static_cast<uint8>(type) << 5) | static_cast<uint8>(recipient));
		bytes.bytes[1] = request;
		bytes.bytes[2] = static_cast<uint8>(value & 0xFF);
		bytes.bytes[3] = static_cast<uint8>(value >> 8);
		bytes.bytes[4] = static_cast<uint8>(index & 0xFF);
		bytes.bytes[5] = static_cast<uint8>(index >> 8);
		bytes.bytes[6] = static_cast<uint8>(length & 0xFF);
		bytes.bytes[7] = static_cast<uint8>(length >> 8);
		return bytes;
	}


	constexpr bool HasDataStage() const noexcept {
		return length != 0;
	}


	// GET_DESCRIPTOR: wValue high byte is the descriptor type, low byte the descriptor index.
	constexpr uint8 GetDescriptorType() const noexcept {
		return static_cast<uint8>(value >> 8);
	}


	constexpr uint8 GetDescriptorIndex() const noexcept {
		return static_cast<uint8>(value & 0xFF);
	}


	// Interface and endpoint recipients: the low byte of wIndex is the interface number or the endpoint address.
	constexpr uint8 GetIndexLow() const noexcept {
		return static_cast<uint8>(index & 0xFF);
	}
};
