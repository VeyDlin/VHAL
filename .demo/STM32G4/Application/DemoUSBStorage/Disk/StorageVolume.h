#pragma once
#include <DemoProducts.h>
#include <Drivers/Memory/BlockDevice/RamBlockDevice.h>
#include <Utilities/FileSystem/Fat12ImageBuilder.h>
#include <string_view>


// The RAM disk and its volume. The disk is the whole medium: the FAT12 image is built at compile time and copied into the disk at start,
// so the host mounts the volume with no formatting. The image is kept in flash, so the disk costs RAM only, and its contents are lost
// at each reset.
//
// Size. The STM32G431 has 22 KB of RAM (SRAM1 16 KB and SRAM2 6 KB, contiguous), and the disk is a member array, so it takes RAM. The rest
// of the static RAM is the console ring, the USB device and the mass storage function with its 2048-byte buffer: .data and .bss are
// 16904 B at 24 blocks, and the linker keeps a 1 KB block for the stack after them. Each block adds 512 B. At 28 blocks .data and .bss
// are 18952 B, and 2552 B stay free beyond the stack block. A count of 29 would leave 2040 B, under the 2 KB margin. The numbers are in
// the README.
//
// The volume at 28 blocks: the boot sector (1 sector), two FAT copies of one sector each, a root directory of 32 entries (2 sectors),
// and 23 clusters of one sector for the data.
namespace DemoUSBStorage {
	constexpr uint32 StorageBlockSize = 512;
	constexpr uint32 StorageBlockCount = 28;

	using StorageDisk = RamBlockDevice<StorageBlockSize, StorageBlockCount>;


	// The README.TXT of the disk. Its first line is the title the PC check looks for, and the static_assert below keeps the two equal.
	constexpr std::string_view StorageReadmeText =
		"VHAL RAM disk demo\r\n"
		"\r\n"
		"This drive is a RAM disk on an STM32G431 board, USB ID 1209:0003.\r\n"
		"Its files are kept in the board's RAM. They are lost when the board\r\n"
		"is reset or loses power.\r\n"
		"\r\n"
		"The demo is in the VHAL repository, in the folder\r\n"
		".demo/STM32G4/Application/DemoUSBStorage/\r\n";
	static_assert(
		StorageReadmeText.find(DemoProducts::StorageReadmeTitle) != std::string_view::npos,
		"README.TXT must contain DemoProducts::StorageReadmeTitle, which the PC check looks for");


	// The volume: the label, and two files that say what the device is. The root directory has 32 entries (2 sectors), which keeps the
	// root small: the label and the two files take three of them, and the rest are free for the host to create files in.
	using StorageImage = Fat12Image<+[] {
		return Fat12ImageBuilder()
			.Sectors(StorageBlockCount)
			.VolumeLabel(DemoProducts::StorageVolumeLabel)
			.RootEntries(32)
			.File("README.TXT", StorageReadmeText)
			.File("INFO.TXT",
				"VHAL demo: USBStorage\r\n"
				"USB ID: 1209:0003\r\n"
				"Vendor: VHAL\r\n"
				"Product: RAM Disk Demo\r\n"
				"Serial number: the MCU unique ID, in hex\r\n");
	}>;
}
