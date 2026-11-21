#include "Commands.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "CheckAudio.h"
#include "CheckComposite.h"
#include "CheckDfu.h"
#include "CheckNetwork.h"
#include "CheckStorage.h"
#include "DemoProducts.h"
#include "DeviceIdentity.h"
#include "DeviceTree.h"
#include "ExitCodes.h"
#include "Report.h"
#include "Text.h"

namespace {

// The demo product a command acts on. The list command has none.
std::uint16_t ProductFor(Command command) {
	if (command == Command::Composite) {
		return DemoProducts::CompositeProductId;
	}

	if (command == Command::Storage) {
		return DemoProducts::StorageProductId;
	}

	if (command == Command::Network) {
		return DemoProducts::NetworkProductId;
	}

	if (command == Command::Audio) {
		return DemoProducts::AudioProductId;
	}

	return DemoProducts::DfuProductId;
}


int RunList() {
	std::vector<BoardInfo> boards;
	std::string error;
	if (!EnumerateBoards(boards, error)) {
		std::fprintf(stderr, "usbclasscheck: %s\n", error.c_str());
		return ExitDevice;
	}

	if (boards.empty()) {
		std::string text;
		AppendFormat(
			text,
			"No demo board is attached (USB IDs %04X:%04X to %04X:%04X).\n",
			static_cast<unsigned int>(DemoProducts::VendorId),
			static_cast<unsigned int>(DemoProducts::BenchProductId),
			static_cast<unsigned int>(DemoProducts::VendorId),
			static_cast<unsigned int>(DemoProducts::AudioProductId));
		std::fputs(text.c_str(), stdout);
		return ExitDevice;
	}

	std::string text = "  PID   demo         serial number\n";
	for (const BoardInfo& board : boards) {
		text += BoardLine(board) + "\n";
	}

	std::fputs(text.c_str(), stdout);
	return ExitOk;
}


// Says why no board was chosen, and lists the boards of the demo when several are attached.
int ReportSelectionFailure(BoardSelection selection, const std::vector<BoardInfo>& boards, std::uint16_t productId, const std::string& serial) {
	std::string demo = DemoNameOf(productId);
	if (selection == BoardSelection::NoBoard) {
		std::printf("No %s board is attached (USB ID 1209:%04X). Connect the board and try again.\n", demo.c_str(), static_cast<unsigned int>(productId));
		return ExitDevice;
	}

	if (selection == BoardSelection::SerialNotFound) {
		std::printf("No demo board with serial '%s' is attached.\n", serial.c_str());
		return ExitDevice;
	}

	if (selection == BoardSelection::SerialOfOtherDemo) {
		std::printf("The board with serial '%s' is not a %s.\n", serial.c_str(), demo.c_str());
		return ExitUsage;
	}

	std::string text;
	for (const BoardInfo& board : boards) {
		if (board.productId == productId) {
			text += BoardLine(board) + "\n";
		}
	}

	std::fprintf(stderr, "usbclasscheck: several %s boards are attached; name one with --serial:\n", demo.c_str());
	std::fputs(text.c_str(), stderr);
	return ExitUsage;
}


int RunChecks(const Options& options) {
	std::uint16_t productId = ProductFor(options.command);
	std::vector<BoardInfo> boards;
	std::string error;
	if (!EnumerateBoards(boards, error)) {
		std::fprintf(stderr, "usbclasscheck: %s\n", error.c_str());
		return ExitDevice;
	}

	std::size_t index = 0;
	BoardSelection selection = SelectBoard(boards, productId, options.serial, index);
	if (selection != BoardSelection::Chosen) {
		return ReportSelectionFailure(selection, boards, productId, options.serial);
	}

	const BoardInfo& board = boards[index];
	std::vector<CheckReport> reports;
	if (options.command == Command::Composite) {
		RunCompositeChecks(options.compositeCheck, board, reports);
	} else if (options.command == Command::Storage) {
		CheckReport report("storage");
		RunStorageCheck(board, report);
		reports.push_back(report);
	} else if (options.command == Command::Network) {
		CheckReport report("network");
		RunNetworkCheck(board, report);
		reports.push_back(report);
	} else if (options.command == Command::Audio) {
		CheckReport report("audio");
		RunAudioCheck(board, report);
		reports.push_back(report);
	} else {
		CheckReport report("dfu");
		RunDfuCheck(board, options.dfuUtil, options.console, report);
		reports.push_back(report);
	}

	std::string serialText = board.serial.empty() ? "(no serial number)" : board.serial;
	std::string text = DemoNameOf(productId) + " board, serial " + serialText + "\n";
	text += FormatReports(reports);
	std::fputs(text.c_str(), stdout);
	return ExitCodeOf(reports);
}

}


int RunCommand(const Options& options) {
	if (options.command == Command::List) {
		return RunList();
	}

	return RunChecks(options);
}
