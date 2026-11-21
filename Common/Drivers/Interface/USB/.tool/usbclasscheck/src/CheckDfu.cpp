#include "CheckDfu.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <windows.h>

#include "CheckData.h"
#include "ConsoleCapture.h"
#include "Crc32.h"
#include "DemoProducts.h"
#include "DeviceIdentity.h"
#include "ProcessRun.h"
#include "Report.h"
#include "StatusAnswer.h"
#include "Text.h"
#include "VolumeAccess.h"
#include "WinText.h"

namespace {

// A region of 32 KB takes a few seconds to erase and write, so one dfu-util transfer may run for a while. The timeout only stops a
// transfer that never ends.
constexpr std::uint32_t TransferTimeoutMs = 120000;
constexpr std::uint32_t VersionTimeoutMs = 10000;
// The console prints its download line when the download completes. The capture runs a little longer than dfu-util, so the
// line is read even when it is printed at the very end.
constexpr DWORD ConsoleSettleMs = 500;


// The first and the last line of a program's output that has text on it. dfu-util ends with its result, and starts with its name.
std::string FirstLine(const std::string& output) {
	std::vector<std::string> lines = SplitLines(output);
	for (const std::string& line : lines) {
		std::string trimmed = TrimSpaces(line);
		if (!trimmed.empty()) {
			return trimmed;
		}
	}

	return "";
}


std::string LastLine(const std::string& output) {
	std::vector<std::string> lines = SplitLines(output);
	for (std::size_t index = lines.size(); index > 0; index--) {
		std::string line = TrimSpaces(lines[index - 1]);
		if (!line.empty()) {
			return line;
		}
	}

	return "";
}


// The path of a file in the Windows temp folder. The wide API is used, so a user folder with characters outside ASCII keeps them. False
// with an error when the folder cannot be found.
bool TempPath(const std::string& name, std::string& path, std::string& error) {
	std::wstring directory(MAX_PATH + 1, L'\0');
	DWORD length = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
	if (length == 0 || length >= directory.size()) {
		error = "the temp folder cannot be found: " + FormatWindowsError(GetLastError());
		return false;
	}

	directory.resize(length);
	path = NarrowUtf8(directory) + name;
	return true;
}


// The files this run created. RemoveAll deletes them, and the destructor deletes what is left, so a run that ends early leaves none
// behind. A file that this run did not create is never added, so it is never deleted.
class CreatedFiles {
private:
	std::vector<std::string> paths;


public:
	CreatedFiles() = default;


	CreatedFiles(const CreatedFiles&) = delete;
	CreatedFiles& operator=(const CreatedFiles&) = delete;


	~CreatedFiles() {
		std::string ignored;
		RemoveAll(ignored);
	}


	void Add(const std::string& path) {
		paths.push_back(path);
	}


	// Deletes the files this run created. False with the first error when one could not be deleted; the others are deleted anyway.
	bool RemoveAll(std::string& error) {
		bool ok = true;
		for (const std::string& path : paths) {
			std::string deleteError;
			if (!DeleteFileAt(path, deleteError)) {
				if (ok) {
					error = deleteError;
				}

				ok = false;
			}
		}

		paths.clear();
		return ok;
	}
};


// Asks dfu-util which DFU device has the board's serial, and gives the text it lists for it. dfu-util compares -S case-sensitively, and the
// serial Windows gives the board may be in another case, so the listed text is the one passed on. False when the step fails; the step is
// added to the report.
bool ListBoardSerial(const std::string& program, const BoardInfo& board, std::string& serial, CheckReport& report) {
	int exitCode = 0;
	std::string output;
	std::string error;
	if (!RunProgram(program, DfuListArguments(DemoProducts::DfuProductId), VersionTimeoutMs, exitCode, output, error)) {
		report.AddStep("dfu-util list", StepStatus::Error, error);
		return false;
	}

	DfuOutcome outcome = ClassifyDfuRun(exitCode, output);
	if (outcome == DfuOutcome::NoDevice) {
		report.AddStep("dfu-util list", StepStatus::Error, "dfu-util does not see the board in DFU mode (is WinUSB bound?): " + LastLine(output));
		return false;
	}

	if (outcome == DfuOutcome::Failed) {
		report.AddStep("dfu-util list", StepStatus::Fail, "dfu-util exit " + std::to_string(exitCode) + ": " + LastLine(output));
		return false;
	}

	std::string pickError;
	if (!PickDfuSerial(output, board.serial, serial, pickError)) {
		report.AddStep("dfu-util list", StepStatus::Error, pickError);
		return false;
	}

	report.AddStep("dfu-util list", StepStatus::Pass, "dfu-util lists the board as serial \"" + serial + "\"");
	return true;
}


// Runs one dfu-util transfer and adds its step. Returns the outcome, so the caller knows whether the transfer completed. serial is the
// text dfu-util lists for the board, or empty when the board has no serial number.
StepStatus Transfer(
	const std::string& program,
	const std::string& serial,
	DfuDirection direction,
	const std::string& filePath,
	const std::string& stepName,
	CheckReport& report)
{
	std::vector<std::string> arguments = DfuArguments(DemoProducts::DfuProductId, serial, direction, filePath);
	int exitCode = 0;
	std::string output;
	std::string error;
	if (!RunProgram(program, arguments, TransferTimeoutMs, exitCode, output, error)) {
		report.AddStep(stepName, StepStatus::Error, error);
		return StepStatus::Error;
	}

	DfuOutcome outcome = ClassifyDfuRun(exitCode, output);
	if (outcome == DfuOutcome::NoDevice) {
		report.AddStep(stepName, StepStatus::Error, "dfu-util does not see the board in DFU mode (is WinUSB bound?): " + LastLine(output));
		return StepStatus::Error;
	}

	if (outcome == DfuOutcome::Failed) {
		report.AddStep(stepName, StepStatus::Fail, "dfu-util exit " + std::to_string(exitCode) + ": " + LastLine(output));
		return StepStatus::Fail;
	}

	report.AddStep(stepName, StepStatus::Pass, std::to_string(DemoProducts::DfuRegionSize) + " bytes");
	return StepStatus::Pass;
}


// The compare step: the upload must be the download, byte for byte.
void CompareUpload(const std::string& uploadPath, const std::vector<std::uint8_t>& sent, CheckReport& report) {
	std::string error;
	std::vector<std::uint8_t> received;
	if (!ReadWholeFile(uploadPath, received, error)) {
		report.AddStep("compare", StepStatus::Error, error);
		return;
	}

	if (received.size() != sent.size()) {
		report.AddStep(
			"compare",
			StepStatus::Fail,
			"the upload has " + std::to_string(received.size()) + " bytes, the download " + std::to_string(sent.size()));
		return;
	}

	std::size_t offset = 0;
	if (FirstMismatch(sent, received, offset)) {
		report.AddStep("compare", StepStatus::Fail, "the bytes differ, first at offset " + std::to_string(offset));
		return;
	}

	report.AddStep("compare", StepStatus::Pass, std::to_string(received.size()) + " bytes identical");
}


// The console step: the board's download line must carry the CRC of the file, and the download must have completed.
void CheckConsoleLine(const std::string& text, const std::string& consolePort, bool downloaded, const std::vector<std::uint8_t>& sent, CheckReport& report) {
	const std::uint32_t regionBytes = DemoProducts::DfuRegionSize;
	std::uint32_t boardCrc = 0;
	if (!FindCrcLine(text, regionBytes, boardCrc)) {
		report.AddStep(
			"console CRC line",
			StepStatus::Fail,
			"no 'download complete bytes=" + std::to_string(regionBytes) + "' line on " + consolePort);
		return;
	}

	std::uint32_t fileCrc = Crc32(sent);
	if (!downloaded || boardCrc != fileCrc) {
		report.AddStep(
			"console CRC line",
			StepStatus::Fail,
			"the board printed " + HexCrc(boardCrc) + ", the file has " + HexCrc(fileCrc));
		return;
	}

	report.AddStep("console CRC line", StepStatus::Pass, "the board printed " + HexCrc(boardCrc));
}

}


void RunDfuCheck(const BoardInfo& board, const std::string& dfuUtilPath, const std::string& consolePort, CheckReport& report) {
	std::string program = dfuUtilPath.empty() ? "dfu-util" : dfuUtilPath;
	std::string output;
	std::string error;
	int exitCode = 0;
	if (!RunProgram(program, {"--version"}, VersionTimeoutMs, exitCode, output, error) || exitCode != 0) {
		report.AddStep("dfu-util", StepStatus::Error, "cannot run " + program + (error.empty() ? "" : ": " + error));
		return;
	}

	report.AddStep("dfu-util", StepStatus::Pass, FirstLine(output));

	// A board with a serial number is selected by the serial text dfu-util lists. A board with none is run without -S, as it was before.
	std::string serial;
	if (!board.serial.empty() && !ListBoardSerial(program, board, serial, report)) {
		return;
	}

	// The names carry this process's ID, the tick count and a random number, so another process is unlikely to pick the same names. CreateNewFile
	// refuses a file that does exist, and only the files this run creates are deleted at the end.
	std::random_device entropy;
	std::uint32_t nonce = entropy();
	std::string stem = "usbclasscheck-dfu-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + "-"
		+ std::to_string(nonce);
	std::string downloadPath;
	std::string uploadPath;
	if (!TempPath(stem + "-download.bin", downloadPath, error) || !TempPath(stem + "-upload.bin", uploadPath, error)) {
		report.AddStep("temporary files", StepStatus::Error, error);
		return;
	}

	CreatedFiles created;
	std::vector<std::uint8_t> sent(DemoProducts::DfuRegionSize);
	FillPattern(sent, nonce);
	if (!CreateNewFile(downloadPath, sent, error)) {
		report.AddStep("write download file", StepStatus::Error, error);
		return;
	}

	created.Add(downloadPath);

	// The console is read from before the download starts, so the line the board prints at its end is caught.
	ConsoleCapture console;
	bool consoleRunning = false;
	if (!consolePort.empty()) {
		if (console.Start(consolePort, error)) {
			consoleRunning = true;
		} else {
			report.AddStep("console", StepStatus::Error, error);
		}
	}

	StepStatus download = Transfer(program, serial, DfuDirection::Download, downloadPath, "download", report);
	if (download == StepStatus::Pass) {
		report.AddStep("CRC-32 of the download", StepStatus::Pass, HexCrc(Crc32(sent)) + " (zlib CRC-32 of the file)");
	}

	// The upload runs only after a completed download. dfu-util creates the upload file itself, and dfu-util 0.11 refuses a file that
	// exists, so the path must be free before the run and the file is not created here. The path is registered for deletion only when it
	// was free before the run: a file that was already there is someone else's, and is never deleted.
	StepStatus upload = StepStatus::Error;
	if (download == StepStatus::Pass) {
		if (PathExists(uploadPath)) {
			report.AddStep("upload", StepStatus::Error, "the upload file " + uploadPath + " already exists, so dfu-util would refuse it");
		} else {
			upload = Transfer(program, serial, DfuDirection::Upload, uploadPath, "upload", report);
			// The path was free before the run, so a file that exists now was created by dfu-util in this run.
			if (PathExists(uploadPath)) {
				created.Add(uploadPath);
			}
		}
	}

	if (upload == StepStatus::Pass) {
		CompareUpload(uploadPath, sent, report);
	}

	if (consoleRunning) {
		Sleep(ConsoleSettleMs);
		CheckConsoleLine(console.Stop(), consolePort, download == StepStatus::Pass, sent, report);
	}

	std::string removeError;
	if (!created.RemoveAll(removeError)) {
		report.AddStep("temporary files", StepStatus::Fail, removeError);
	}
}
