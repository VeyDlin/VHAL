#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "CheckData.h"
#include "DemoProducts.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


void CheckStorageSizes() {
	Expect(StorageTestFileSize(0, 512) == 0, "no free space gives no test file");
	Expect(StorageTestFileSize(512, 512) == 0, "one sector free gives no test file");
	Expect(StorageTestFileSize(1024, 512) == 512, "two sectors free leave one sector spare");
	Expect(StorageTestFileSize(10752, 512) == 10240, "the demo's free space gives whole sectors, one spare");
	Expect(StorageTestFileSize(10751, 512) == 9728, "a partial sector is not used, and the spare sector is kept");
	Expect(StorageTestFileSize(16ULL * 1024 * 1024, 512) == StorageTestMaxBytes, "a big disk is capped at the maximum");
	Expect(StorageTestFileSize(10752, 512) % 512 == 0, "the size is a whole number of sectors");
	Expect(StorageTestFileSize(16384, 4096) == 12288, "a disk with 4 KB sectors gives whole 4 KB sectors, one spare");
	Expect(StorageTestFileSize(16ULL * 1024 * 1024, 4096) % 4096 == 0, "the cap is a whole number of 4 KB sectors");
	Expect(StorageTestFileSize(10752, 0) == 0, "an unknown sector size gives no test file");
}


void CheckStoragePhrases() {
	Expect(StorageReadmePhrase() == "VHAL RAM disk demo", "the README phrase is the demo's first line");
	Expect(StorageReadmePhrase() == DemoProducts::StorageReadmeTitle, "the README phrase is the title in DemoProducts.h");
	Expect(StorageInfoPhrase() == "USB ID: 1209:0003", "the INFO phrase is the storage demo's USB ID");
	Expect(IsStorageDemo("VHAL    ", "RAM Disk Demo"), "the padded vendor and the product are the demo's");
	Expect(!IsStorageDemo("Kingston", "DataTraveler"), "another stick is not the demo");
	Expect(!IsStorageDemo("VHAL", "RAM Disk"), "a different product string is not the demo's");
}


void CheckPattern() {
	std::vector<std::uint8_t> first(256);
	std::vector<std::uint8_t> second(256);
	FillPattern(first, 42);
	FillPattern(second, 42);
	Expect(first == second, "the same seed gives the same bytes");

	std::vector<std::uint8_t> other(256);
	FillPattern(other, 43);
	Expect(first != other, "another seed gives other bytes");

	bool allSame = true;
	for (std::uint8_t value : first) {
		if (value != first[0]) {
			allSame = false;
		}
	}

	Expect(!allSame, "the pattern is not one repeated byte");
}


void CheckMismatch() {
	std::vector<std::uint8_t> expected = { 1, 2, 3, 4 };
	std::vector<std::uint8_t> same = { 1, 2, 3, 4 };
	std::vector<std::uint8_t> changed = { 1, 2, 9, 4 };
	std::vector<std::uint8_t> shorter = { 1, 2, 3 };
	std::size_t offset = 99;

	Expect(!FirstMismatch(expected, same, offset), "equal blocks have no mismatch");
	Expect(FirstMismatch(expected, changed, offset) && offset == 2, "the first differing byte is found");
	Expect(FirstMismatch(expected, shorter, offset) && offset == 3, "a shorter block differs at its end");
}


void CheckDfuArguments() {
	std::vector<std::string> download = DfuArguments(DemoProducts::DfuProductId, "", DfuDirection::Download, "C:/tmp/a.bin");
	std::vector<std::string> expected = { "-d", "1209:0004", "-a", "0", "-D", "C:/tmp/a.bin" };
	Expect(download == expected, "a download without a serial uses the DFU ID, alternate 0 and -D");

	std::vector<std::string> upload = DfuArguments(DemoProducts::DfuProductId, "0A1B", DfuDirection::Upload, "b.bin");
	std::vector<std::string> expectedUpload = { "-d", "1209:0004", "-a", "0", "-S", "0A1B", "-U", "b.bin" };
	Expect(upload == expectedUpload, "an upload with a serial selects the board with -S and reads with -U");

	Expect(
		CommandLineOf({ "C:/Program Files/dfu/dfu-util.exe", "-d", "1209:0004" }) == "\"C:/Program Files/dfu/dfu-util.exe\" -d 1209:0004",
		"arguments with spaces are quoted");
}


void CheckDfuSerialPick() {
	const std::string boardLine =
		"Found DFU: [1209:0004] ver=0100, devnum=63, cfg=1, intf=0, path=\"2-1\", alt=0, name=\"Upper flash 0x08018000 32 KB\", "
		"serial=\"22004F000950303448383720\"\n";
	const std::string upper = "22004F000950303448383720";

	Expect(DfuListArguments(DemoProducts::DfuProductId) == std::vector<std::string>({ "-l", "-d", "1209:0004" }), "the list runs dfu-util -l on the DFU ID");

	std::vector<std::string> listed = DfuListedSerials(boardLine);
	Expect(listed == std::vector<std::string>({ upper }), "the serial= field of the line is read");
	Expect(DfuListedSerials("No DFU capable USB device available\n").empty(), "a listing with no device has no serial");

	std::vector<std::string> twoSettings = DfuListedSerials(boardLine + boardLine);
	Expect(twoSettings.size() == 2, "a device listed for two alternate settings gives a serial for each line");

	std::string serial;
	std::string error;
	Expect(PickDfuSerial(boardLine, "22004f000950303448383720", serial, error), "a lower-case board serial is found in the listing");
	Expect(serial == upper, "the picked serial is the listed text, upper case, as dfu-util compares it");
	Expect(error.empty(), "a found serial gives no error");

	Expect(PickDfuSerial(boardLine, upper, serial, error) && serial == upper, "the listed text picks itself");

	Expect(!PickDfuSerial(boardLine, "0A1B2C", serial, error), "a board that is not listed is an error");
	Expect(serial.empty(), "an error leaves no serial");
	Expect(error.find("lists no DFU device") != std::string::npos, "the error says no device has the board's serial");

	Expect(!PickDfuSerial("No DFU capable USB device available\n", upper, serial, error), "no listed device is an error");
	Expect(error.find("lists no DFU device") != std::string::npos, "no device gives the same reason");

	std::string twoDevices =
		"Found DFU: [1209:0004] ver=0100, devnum=63, cfg=1, intf=0, path=\"2-1\", alt=0, name=\"x\", serial=\"ABC123\"\n"
		"Found DFU: [1209:0004] ver=0100, devnum=64, cfg=1, intf=0, path=\"2-2\", alt=0, name=\"x\", serial=\"abc123\"\n";
	Expect(!PickDfuSerial(twoDevices, "ABC123", serial, error), "two devices with the serial in different case are an error");
	Expect(error.find("2 DFU devices") != std::string::npos, "the error counts the devices");

	std::string sameSettings = boardLine + boardLine;
	Expect(PickDfuSerial(sameSettings, upper, serial, error) && serial == upper, "one device with two alternate settings is still one device");

	std::string mixed = boardLine + "Found DFU: [1209:0004] ver=0100, devnum=65, cfg=1, intf=0, path=\"2-3\", alt=0, name=\"x\", serial=\"0A1B2C\"\n";
	Expect(PickDfuSerial(mixed, "0a1b2c", serial, error) && serial == "0A1B2C", "the board is picked among other devices");

	Expect(!PickDfuSerial(boardLine, "", serial, error), "a board with no serial has nothing to pick");
}


void CheckDfuOutcome() {
	Expect(ClassifyDfuRun(0, "Download done.\n") == DfuOutcome::Ok, "exit 0 is ok");
	Expect(ClassifyDfuRun(74, "Error during download\n") == DfuOutcome::Failed, "a failed transfer is a failure");
	Expect(
		ClassifyDfuRun(1, "dfu-util: No DFU capable USB device available\n") == DfuOutcome::NoDevice,
		"no DFU device is a device problem");
	Expect(
		ClassifyDfuRun(1, "Cannot open DFU device 1209:0004\n") == DfuOutcome::NoDevice,
		"a DFU device that cannot be opened is a device problem");
}


void CheckCrcLine() {
	std::uint32_t crc = 0;
	const std::string line = "t=5000ms usb=Configured\r\ndownload complete bytes=32768 crc32=0x1A2B3C4D\r\n";
	Expect(FindCrcLine(line, 32768, crc), "the download line is found");
	Expect(crc == 0x1A2B3C4DU, "the CRC of the download line is read");

	std::uint32_t lower = 0;
	Expect(FindCrcLine("download complete bytes=32768 crc32=0x1a2b3c4d\n", 32768, lower) && lower == 0x1A2B3C4DU, "the hex may be lower case");

	Expect(!FindCrcLine(line, 1024, crc), "a line for another byte count is not taken");
	Expect(!FindCrcLine("download complete bytes=32768 crc32=0x1A2B3C", 32768, crc), "a truncated CRC is refused");
	Expect(!FindCrcLine("download complete bytes=32768 crc32=0xZZZZZZZZ", 32768, crc), "a CRC that is not hex is refused");
	Expect(HexCrc(0x1A2B3C4DU) == "0x1A2B3C4D", "a CRC prints as 0x and eight hex digits");
	Expect(HexCrc(0x0000ABCDU) == "0x0000ABCD", "a short CRC is padded to eight digits");
}


}


int main() {
	CheckStorageSizes();
	CheckStoragePhrases();
	CheckPattern();
	CheckMismatch();
	CheckDfuArguments();
	CheckDfuSerialPick();
	CheckDfuOutcome();
	CheckCrcLine();

	std::printf("usbclasscheck check data test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
