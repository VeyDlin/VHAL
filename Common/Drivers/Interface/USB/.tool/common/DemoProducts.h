#pragma once
#include <cstdint>


// The identities of the demos on the STM32G431 board (.demo/STM32G4), shared with the PC tools that drive them. Both sides include this
// header, so it uses only <cstdint> and builds for the MCU and for the PC.
//
// The IDs are pid.codes test values (VID 0x1209, PIDs 0x0001 to 0x0010 are test PIDs). They are for these tools only: never ship a
// product with them. Each demo has its own PID, because Windows caches the descriptors of a VID and PID pair, so a demo that changed its
// descriptors under an old PID would show the old ones.
struct DemoProducts {
	static constexpr std::uint16_t VendorId = 0x1209;
	// The benchmark. BenchProtocol.h has the same value, and BenchDescriptors.h asserts that the two match.
	static constexpr std::uint16_t BenchProductId = 0x0001;
	static constexpr std::uint16_t CompositeProductId = 0x0002;
	// The mass storage demo (DemoUSBStorage).
	static constexpr std::uint16_t StorageProductId = 0x0003;
	// The DFU demo (DemoUSBDfu), in DFU mode.
	static constexpr std::uint16_t DfuProductId = 0x0004;
	static constexpr std::uint16_t DeviceRelease = 0x0100;


	// Device interface GUIDs for WinUSB. The Microsoft OS 2.0 descriptor names one, and the PC tool opens the device with it.
	static constexpr const char* CompositeInterfaceGuid = "{53B72DEF-1477-422A-B645-D5267E3E44A6}";
	static constexpr const char* DfuInterfaceGuid = "{21056BA5-792A-4C41-B661-56F405F35175}";

	// The DFU demo's memory: the top 32 KB of flash, the part its image does not use. DfuRegionTarget.h asserts that the demo's CMake
	// values (VHAL_DFU_REGION_START and VHAL_DFU_REGION_SIZE) match these, and the PC tool writes a file of this size.
	static constexpr std::uint32_t DfuRegionStart = 0x08018000;
	static constexpr std::uint32_t DfuRegionSize = 0x8000;

	// The mass storage demo as the host shows it: the volume label, and the vendor and product strings of the disk. The demo uses these
	// constants (StorageVolume.h and StorageDescriptors.h), so the text the host sees is the text the PC tool checks.
	static constexpr const char* StorageVolumeLabel = "VHAL DEMO";
	static constexpr const char* StorageVendor = "VHAL";
	static constexpr const char* StorageProduct = "RAM Disk Demo";
	// The first line of the storage demo's README.TXT. The PC check looks for it in the disk's README.TXT, and StorageVolume.h asserts
	// that the README text the demo builds contains it.
	static constexpr const char* StorageReadmeTitle = "VHAL RAM disk demo";


	// Vendor code of the composite's Microsoft OS 2.0 descriptor request (bmRequestType 0xC0, wIndex 7). The benchmark uses 0x20.
	// The composite uses another value, so a request meant for one demo is never answered by the other one's descriptors in a trace or
	// by a PC tool that talks to both.
	static constexpr std::uint8_t CompositeMsOs20VendorCode = 0x21;
	// Vendor code of the DFU demo's Microsoft OS 2.0 descriptor request. It differs from the bench (0x20) and the composite (0x21) for
	// the same reason: a request meant for one demo is never answered by another demo's descriptors.
	static constexpr std::uint8_t DfuMsOs20VendorCode = 0x22;


	// Where the composite's vendor raw channel sits. The builder numbers the endpoints in the order of the functions: keyboard (interface 0,
	// IN 0x81), mouse (interface 1, IN 0x82), serial port (interfaces 2 and 3: notification IN 0x83, bulk IN 0x84, bulk OUT 0x05), vendor
	// channel (interface 4, bulk IN 0x86, bulk OUT 0x07). CompositeDescriptors.h asserts these values against the built bytes.
	static constexpr std::uint8_t CompositeVendorInterface = 4;
	static constexpr std::uint8_t CompositeVendorBulkInAddress = 0x86;
	static constexpr std::uint8_t CompositeVendorBulkOutAddress = 0x07;
	// The serial port's CDC-ACM control interface (interface 2). The COM port that Windows creates for the serial port is found on it.
	static constexpr std::uint8_t CompositeSerialControlInterface = 2;


	// The network demo (DemoUSBNetwork): a CDC-NCM Ethernet link with a minimal IPv4 responder (ARP, ICMP echo and DHCP). The board
	// answers on NetworkDeviceAddress, and its DHCP server leases NetworkLeasedAddress to the host. The addresses are IPv4 as 32-bit
	// values, the first octet in the most significant byte: 192.168.7.1 is 0xC0A80701.
	static constexpr std::uint16_t NetworkProductId = 0x0005;
	static constexpr std::uint32_t NetworkDeviceAddress = 0xC0A80701;
	static constexpr std::uint32_t NetworkLeasedAddress = 0xC0A80702;
	static constexpr std::uint32_t NetworkNetmask = 0xFFFFFF00;


	// The audio demo (DemoUSBAudio): a USB Audio 1.0 device with a mono microphone and a stereo speaker, both at AudioSampleRate. While
	// the speaker is not playing, the microphone sends a sine of AudioToneFrequency at AudioToneAmplitude (the peak, in LSBs of the 16-bit
	// samples). While the speaker plays, the microphone sends the speaker's audio back (a loopback). The PC tool uses the same values to
	// check the tone and the loopback.
	static constexpr std::uint16_t AudioProductId = 0x0006;
	static constexpr std::uint32_t AudioSampleRate = 48000;
	static constexpr std::uint32_t AudioToneFrequency = 1000;
	static constexpr std::int16_t AudioToneAmplitude = 8000;


	// Derives the two MAC addresses of the network demo from the MCU's 12-byte unique ID, the bytes the serial number is the hex text of.
	// hostMac, the host's own adapter (iMACAddress), comes from bytes 0 to 5. deviceMac, the board's address, comes from bytes 6 to 11.
	// Both are locally administered unicast: bit 1 of the first byte is set and bit 0 is clear. If the two came out equal, the last byte
	// of deviceMac gets 1 added, so they always differ. The PC tool computes the same MACs from the serial number.
	static constexpr void NetworkMacsFromUniqueId(const std::uint8_t* uniqueId, std::uint8_t* hostMac, std::uint8_t* deviceMac) {
		for (std::uint8_t index = 0; index < 6; index++) {
			hostMac[index] = uniqueId[index];
			deviceMac[index] = uniqueId[6 + index];
		}

		hostMac[0] = static_cast<std::uint8_t>((hostMac[0] & 0xFE) | 0x02);
		deviceMac[0] = static_cast<std::uint8_t>((deviceMac[0] & 0xFE) | 0x02);
		bool equal = true;
		for (std::uint8_t index = 0; index < 6; index++) {
			if (hostMac[index] != deviceMac[index]) {
				equal = false;
			}
		}

		if (equal) {
			deviceMac[5] = static_cast<std::uint8_t>(deviceMac[5] + 1);
		}
	}
};


// The command words of the composite's command line on its serial port. The PC tool sends the same words.
struct CompositeCommands {
	static constexpr const char* Help = "help";
	static constexpr const char* Type = "type";
	static constexpr const char* Key = "key";
	static constexpr const char* Move = "move";
	static constexpr const char* Click = "click";
	static constexpr const char* Wheel = "wheel";
	static constexpr const char* Status = "status";
};
