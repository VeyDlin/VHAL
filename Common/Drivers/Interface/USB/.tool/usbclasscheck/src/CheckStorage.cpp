#include "CheckStorage.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "CheckData.h"
#include "DemoProducts.h"
#include "DeviceIdentity.h"
#include "DeviceTree.h"
#include "Report.h"
#include "Text.h"
#include "VolumeAccess.h"

namespace {

// The test file goes in the root of the volume and is deleted at the end. The name is unlikely to clash with a file on the disk,
// and a file that already has it is not overwritten.
const char* TestFileName = "VHALCHK.BIN";


// How a volume with the storage label relates to this board. Unresolved means the chain from the volume to its USB device could not
// be read, so the volume cannot be shown to be this board's, and it is never written to.
enum class VolumeMatch : std::uint8_t {
	Board,
	NotBoard,
	Unresolved
};


std::string VolumeText(const DiskIdentity& identity) {
	return "vendor '" + identity.vendor + "', product '" + identity.product + "', serial '" + identity.serial + "'";
}


// Decides whether the volume at root is the storage demo of this board. The volume's disk is found by its disk number, the disk's
// device node by its parents, and the USB device node above the disk must be this board (IsBoardStorageNode). The disk's SCSI strings
// must be the demo's too. reason says why the volume is not the board's, or why it could not be decided.
VolumeMatch MatchVolume(const std::string& root, const BoardInfo& board, std::string& reason) {
	DiskNumber number;
	std::string error;
	if (!DiskNumberOfVolume(root, number, error)) {
		reason = error;
		return VolumeMatch::Unresolved;
	}

	bool diskFound = false;
	std::string diskInstance;
	if (!FindDiskInstance(number, diskInstance, diskFound, error)) {
		reason = error;
		return VolumeMatch::Unresolved;
	}

	if (!diskFound) {
		reason = "its disk is not in the device tree";
		return VolumeMatch::Unresolved;
	}

	bool nodeFound = false;
	std::string node;
	if (!UsbDeviceNodeAbove(diskInstance, node, nodeFound, error)) {
		reason = error;
		return VolumeMatch::Unresolved;
	}

	if (!nodeFound) {
		reason = "its disk is not on a USB device";
		return VolumeMatch::NotBoard;
	}

	std::string nodeReason;
	if (!IsBoardStorageNode(node, board, nodeReason)) {
		reason = nodeReason;
		return VolumeMatch::NotBoard;
	}

	DiskIdentity identity;
	if (!ReadDiskIdentity(root, identity, error)) {
		reason = error;
		return VolumeMatch::Unresolved;
	}

	if (!IsStorageDemo(identity.vendor, identity.product)) {
		reason = VolumeText(identity) + " is not the storage demo";
		return VolumeMatch::NotBoard;
	}

	return VolumeMatch::Board;
}

}


void RunStorageCheck(const BoardInfo& board, CheckReport& report) {
	// Without the board's serial the disk cannot be tied to this board, so nothing is written.
	if (board.serial.empty()) {
		report.AddStep(
			"disk identity",
			StepStatus::Error,
			"the board has no serial number, so its disk cannot be told from another board's: nothing is written");
		return;
	}

	std::string error;
	std::vector<std::string> roots;
	if (!FindVolumesLabelled(DemoProducts::StorageVolumeLabel, roots, error)) {
		report.AddStep("volume", StepStatus::Error, error);
		return;
	}

	if (roots.empty()) {
		report.AddStep(
			"volume",
			StepStatus::Error,
			std::string("no mounted volume is labelled '") + DemoProducts::StorageVolumeLabel + "' (is the board plugged in and mounted?)");
		return;
	}

	// Several volumes can carry the label. The one that is this board's storage demo is the one whose disk is on this board.
	std::string root;
	std::string rejected;
	std::string unresolved;
	for (const std::string& candidate : roots) {
		std::string reason;
		VolumeMatch match = MatchVolume(candidate, board, reason);
		if (match == VolumeMatch::Board) {
			root = candidate;
			break;
		}

		if (match == VolumeMatch::Unresolved) {
			unresolved += candidate + " (" + reason + "); ";
		} else {
			rejected += candidate + " (" + reason + "); ";
		}
	}

	if (root.empty()) {
		if (!unresolved.empty()) {
			report.AddStep("disk identity", StepStatus::Error, "a volume could not be tied to this board, so nothing is written: " + unresolved);
		} else {
			report.AddStep("disk identity", StepStatus::Fail, "no disk of this board carries the label: " + rejected);
		}

		return;
	}

	report.AddStep("volume", StepStatus::Pass, root + " labelled '" + DemoProducts::StorageVolumeLabel + "'");
	report.AddStep("disk identity", StepStatus::Pass, "the storage demo of this board, serial " + board.serial);

	std::vector<std::uint8_t> bytes;
	if (!ReadWholeFile(root + "README.TXT", bytes, error)) {
		report.AddStep("README.TXT", StepStatus::Error, error);
	} else {
		std::string text(bytes.begin(), bytes.end());
		std::string phrase = StorageReadmePhrase();
		report.AddStep(
			"README.TXT",
			text.find(phrase) != std::string::npos ? StepStatus::Pass : StepStatus::Fail,
			"expected the text '" + phrase + "'");
	}

	if (!ReadWholeFile(root + "INFO.TXT", bytes, error)) {
		report.AddStep("INFO.TXT", StepStatus::Error, error);
	} else {
		std::string text(bytes.begin(), bytes.end());
		std::string phrase = StorageInfoPhrase();
		report.AddStep(
			"INFO.TXT",
			text.find(phrase) != std::string::npos ? StepStatus::Pass : StepStatus::Fail,
			"expected the text '" + phrase + "'");
	}

	std::uint64_t freeBytes = 0;
	std::uint64_t sectorBytes = 0;
	if (!FreeSpaceOf(root, freeBytes, sectorBytes, error)) {
		report.AddStep("free space", StepStatus::Error, error);
		return;
	}

	std::uint64_t testBytes = StorageTestFileSize(freeBytes, sectorBytes);
	if (testBytes == 0) {
		report.AddStep(
			"free space",
			StepStatus::Error,
			"no room for a test file (" + std::to_string(freeBytes) + " bytes free, sectors of " + std::to_string(sectorBytes) + " bytes)");
		return;
	}

	std::string testPath = root + TestFileName;
	std::random_device entropy;
	std::vector<std::uint8_t> sent(static_cast<std::size_t>(testBytes));
	FillPattern(sent, entropy());
	// CreateNewFile removes the file again if its write fails, so a failed write leaves nothing on the volume.
	if (!CreateNewFile(testPath, sent, error)) {
		report.AddStep("write test file", StepStatus::Error, error);
		return;
	}

	report.AddStep("write test file", StepStatus::Pass, std::to_string(testBytes) + " bytes, flushed");

	// The read-back and the delete both run after a successful write, so the volume gets its test file removed either way.
	std::vector<std::uint8_t> received;
	if (!ReadBackUncached(testPath, sent.size(), received, error)) {
		report.AddStep("read back uncached", StepStatus::Error, error);
	} else {
		std::size_t offset = 0;
		if (FirstMismatch(sent, received, offset)) {
			report.AddStep("read back uncached", StepStatus::Fail, "the bytes differ, first at offset " + std::to_string(offset));
		} else {
			report.AddStep("read back uncached", StepStatus::Pass, std::to_string(received.size()) + " bytes identical");
		}
	}

	if (!DeleteFileAt(testPath, error)) {
		report.AddStep("delete test file", StepStatus::Fail, error);
	} else {
		report.AddStep("delete test file", StepStatus::Pass, "the volume is as it was");
	}
}
