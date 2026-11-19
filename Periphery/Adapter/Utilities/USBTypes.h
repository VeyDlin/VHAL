#pragma once
#include <System/System.h>


// Types shared by the USB device controller adapter and the Common device core. They hold no heap memory and can be
// passed by value from an interrupt handler.


enum class USBEndpointType : uint8 {
	Control,
	Isochronous,
	Bulk,
	Interrupt
};


// Full speed is the only speed of the current ports (no external PHY). High is reserved for a port with a ULPI PHY.
enum class USBSpeed : uint8 {
	Low,
	Full,
	High
};


// The 8-byte SETUP packet as received, byte for byte. USBSetupPacket::Parse() in Drivers/Interface/USB/USBProtocol.h
// decodes it, so the adapter does not need to know the request layout.
struct USBSetupBytes {
	static constexpr uint8 Size = 8;
	uint8 bytes[Size] = {};
};


// USB endpoint addresses: bit 7 is the direction (set for IN), bits 3..0 the endpoint number. 0x81 is endpoint 1 IN.
// Endpoint 0 has the addresses 0x00 (OUT) and 0x80 (IN); both belong to the one control pipe.
struct USBEndpoint {
	static constexpr uint8 DirectionInBit = 0x80;
	static constexpr uint8 NumberMask = 0x0F;
	static constexpr uint8 MaxNumber = 15;
	static constexpr uint8 ControlOut = 0x00;
	static constexpr uint8 ControlIn = 0x80;


	static constexpr uint8 Number(uint8 address) noexcept {
		return static_cast<uint8>(address & NumberMask);
	}


	static constexpr bool IsIn(uint8 address) noexcept {
		return (address & DirectionInBit) != 0;
	}


	// Both control addresses, 0x00 and 0x80, and nothing else.
	static constexpr bool IsControl(uint8 address) noexcept {
		return address == ControlOut || address == ControlIn;
	}


	// A well-formed endpoint address: bits 6..4 are reserved and must be zero.
	static constexpr bool IsValid(uint8 address) noexcept {
		return (address & 0x70) == 0;
	}


	static constexpr uint8 MakeIn(uint8 number) noexcept {
		return static_cast<uint8>((number & NumberMask) | DirectionInBit);
	}


	static constexpr uint8 MakeOut(uint8 number) noexcept {
		return static_cast<uint8>(number & NumberMask);
	}
};
