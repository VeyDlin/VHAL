#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The largest test file. The demo's disk is 14 KB, so this only matters on a bigger disk.
inline constexpr std::uint64_t StorageTestMaxBytes = 1024 * 1024;

// The size of the test file for a volume with this many free bytes and this sector size (GetDiskFreeSpaceW): whole sectors, one
// sector kept free, at most the maximum. The uncached read needs whole sectors, so the size is a multiple of the sector size. Zero
// when the volume has no room for one, or the sector size is unknown.
std::uint64_t StorageTestFileSize(std::uint64_t freeBytes, std::uint64_t sectorBytes);

// The text the storage demo's README.TXT contains (its title, DemoProducts::StorageReadmeTitle), and the one its INFO.TXT contains
// (the USB ID line).
std::string StorageReadmePhrase();

std::string StorageInfoPhrase();

// True when a storage device's vendor and product strings are the demo's. The host pads the vendor with spaces.
bool IsStorageDemo(const std::string& vendor, const std::string& product);

// Fills data with a pseudo-random pattern for the seed. The same seed gives the same bytes.
void FillPattern(std::vector<std::uint8_t>& data, std::uint32_t seed);

// Finds the first byte that differs. Different sizes count as a difference at the shorter size.
bool FirstMismatch(const std::vector<std::uint8_t>& expected, const std::vector<std::uint8_t>& actual, std::size_t& offset);

enum class DfuDirection : std::uint8_t {
	Download,
	Upload
};

enum class DfuOutcome : std::uint8_t {
	Ok,
	// dfu-util did not find the board in DFU mode, or could not open it (WinUSB not bound).
	NoDevice,
	Failed
};

// The dfu-util arguments of one transfer: the product, the alternate setting and the file. A serial number selects the
// board when several are attached.
std::vector<std::string> DfuArguments(
	std::uint16_t productId,
	const std::string& serial,
	DfuDirection direction,
	const std::string& filePath);

// The dfu-util arguments that list the DFU devices of the product: dfu-util -l -d 1209:0004.
std::vector<std::string> DfuListArguments(std::uint16_t productId);

// The serial numbers that dfu-util -l prints, one per serial="..." field, in the order they are listed. A device with several alternate
// settings is listed once for each, so one serial can come more than once.
std::vector<std::string> DfuListedSerials(const std::string& output);

// Picks the serial to pass to dfu-util -S for the board. dfu-util compares -S case-sensitively, so the text is the one dfu-util lists,
// which may differ in case from the board's serial. False with the reason in error when no listed device has the board's serial, or
// when several different listed texts match it.
bool PickDfuSerial(const std::string& listOutput, const std::string& boardSerial, std::string& serial, std::string& error);

// The command line of a program and its arguments, with quotes around the arguments that contain spaces.
std::string CommandLineOf(const std::vector<std::string>& arguments);

DfuOutcome ClassifyDfuRun(int exitCode, const std::string& output);

// Finds the line "download complete bytes=N crc32=0x........" that the DFU demo prints for a download of N bytes, and reads its CRC.
bool FindCrcLine(const std::string& text, std::uint32_t bytes, std::uint32_t& crc);

// The CRC as the demo prints it: 0x and eight hex digits.
std::string HexCrc(std::uint32_t crc);
