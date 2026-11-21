#include "CheckComposite.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <windows.h>

#include "CheckData.h"
#include "DemoProducts.h"
#include "DeviceIdentity.h"
#include "DeviceTree.h"
#include "RawInputListener.h"
#include "RawInputLogic.h"
#include "Report.h"
#include "SerialPort.h"
#include "StatusAnswer.h"
#include "Text.h"
#include "WinText.h"
#include "WinUsbVendor.h"

namespace {

// A command of the command line answers within this time. The device answers from its main loop, so a second is far more than it needs.
constexpr std::uint32_t CommandTimeoutMs = 3000;
// After DTR is raised the board prints its greeting from the main loop; this waits for it before the first command.
constexpr std::uint32_t SettleMs = 500;
// The time the key and motion checks let the host deliver the events of one command before the next command is sent.
constexpr std::uint32_t KeyPumpMs = 250;
constexpr std::uint32_t MotionPumpMs = 300;

// The block sizes of the vendor loopback. 1 and 65 are not whole packets, 63 is one byte short of one, and 65536 is a long run.
const std::uint32_t VendorBlockSizes[] = {1, 63, 64, 65, 4096, 65536};


// Sends one command and reads its answer. Pass: the answer has the echo and the prompt, and its body is in body. Fail: the
// device answered something else. Error: the device did not answer, or the port failed.
StepStatus Exchange(SerialPort& port, const std::string& command, std::vector<std::string>& body, std::string& detail) {
	std::string error;
	if (!port.Write(command + "\r", error)) {
		detail = error;
		return StepStatus::Error;
	}

	std::string answer;
	if (!port.ReadAnswer(answer, CommandTimeoutMs, error)) {
		detail = error;
		return StepStatus::Error;
	}

	if (!CommandBody(answer, command, body, detail)) {
		return StepStatus::Fail;
	}

	return StepStatus::Pass;
}


// Reads the status command and parses its fields.
StepStatus ReadStatus(SerialPort& port, CompositeStatus& status, std::string& detail) {
	std::vector<std::string> body;
	StepStatus exchange = Exchange(port, "status", body, detail);
	if (exchange != StepStatus::Pass) {
		return exchange;
	}

	if (!ParseStatus(body, status, detail)) {
		return StepStatus::Fail;
	}

	return StepStatus::Pass;
}


// The COM port of the board's serial interface: the Ports device on the CDC control interface, below this board.
bool FindSerialPort(const BoardInfo& board, std::string& portName, bool& found, std::string& error) {
	std::vector<PortInfo> ports;
	if (!EnumeratePorts(ports, error)) {
		return false;
	}

	found = false;
	for (const PortInfo& port : ports) {
		InstanceParts parts;
		if (!ParseInstance(port.instance, parts)) {
			continue;
		}

		if (parts.vendorId != DemoProducts::VendorId || parts.productId != board.productId) {
			continue;
		}

		if (parts.interfaceNumber != DemoProducts::CompositeSerialControlInterface) {
			continue;
		}

		std::string boardSerial;
		bool boardFound = false;
		if (!SerialOfInstance(port.instance, boardSerial, boardFound, error)) {
			return false;
		}

		if (boardFound && SerialMatches(board.serial, boardSerial)) {
			portName = port.portName;
			found = true;
			return true;
		}
	}

	return true;
}


// Opens the command line of the board and raises DTR, which the demo needs before it prints or accepts commands. Adds the
// steps for it; false when the check cannot go on.
bool OpenCommandLine(const BoardInfo& board, SerialPort& port, CheckReport& report) {
	std::string portName;
	std::string error;
	bool found = false;
	if (!FindSerialPort(board, portName, found, error)) {
		report.AddStep("COM port", StepStatus::Error, error);
		return false;
	}

	if (!found) {
		report.AddStep("COM port", StepStatus::Error, "no COM port on the board's serial interface (Windows did not bind the serial driver)");
		return false;
	}

	if (!port.Open(portName, error) || !port.SetDtr(true, error)) {
		report.AddStep("COM port", StepStatus::Error, error);
		return false;
	}

	// Raising DTR makes the board print its greeting and a prompt. They are read here and dropped, so the first answer
	// is not taken from them.
	std::string greeting;
	if (!port.ReadFor(greeting, SettleMs, error)) {
		report.AddStep("COM port", StepStatus::Error, error);
		return false;
	}

	port.DiscardInput();
	report.AddStep("COM port", StepStatus::Pass, portName + ", DTR raised");
	return true;
}


// The DTR line as a status answer reports it. The demo prints only while DTR is set, so every status read checks it.
void AddDtrStep(const CompositeStatus& status, const std::string& name, CheckReport& report) {
	report.AddStep(name, status.dtr ? StepStatus::Pass : StepStatus::Fail, status.dtr ? "dtr=true" : "dtr=false");
}


// The pointer position, saved before the motion commands and put back afterwards. Restore() puts it back and says whether that
// worked. The destructor puts it back as well when Restore() was not called, so no exit path leaves the pointer moved.
class PointerPosition {
private:
	POINT saved {};
	bool active = false;


public:
	PointerPosition() = default;


	PointerPosition(const PointerPosition&) = delete;
	PointerPosition& operator=(const PointerPosition&) = delete;


	~PointerPosition() {
		std::string ignored;
		Restore(ignored);
	}


	// Saves the position. False with an error when it cannot be read, and then no motion is sent.
	bool Save(std::string& error) {
		if (!GetCursorPos(&saved)) {
			error = "GetCursorPos failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		active = true;
		return true;
	}


	// Puts the pointer back where Save found it. False with an error when Windows refuses the move.
	bool Restore(std::string& error) {
		if (!active) {
			return true;
		}

		active = false;
		if (!SetCursorPos(saved.x, saved.y)) {
			error = "SetCursorPos failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		return true;
	}
};


void RunCdc(const BoardInfo& board, CheckReport& report) {
	SerialPort port;
	if (!OpenCommandLine(board, port, report)) {
		return;
	}

	// The echo of the command and the prompt after the answer are checked once, on the first exchange.
	std::vector<std::string> body;
	std::string detail;
	StepStatus echo = Exchange(port, "status", body, detail);
	if (echo == StepStatus::Pass) {
		detail = "the command is echoed and the prompt follows the answer";
	}

	report.AddStep("echo and prompt", echo, detail);
	if (echo != StepStatus::Pass) {
		return;
	}

	CompositeStatus firstStatusFields;
	if (!ParseStatus(body, firstStatusFields, detail)) {
		report.AddStep("status fields", StepStatus::Fail, detail);
		return;
	}

	AddDtrStep(firstStatusFields, "DTR seen by the device", report);

	const LineCoding codings[2] = {
		{57600, 8, "none", "1"},
		{9600, 7, "even", "1"}
	};
	for (const LineCoding& coding : codings) {
		std::string name = "line coding " + DescribeLineCoding(coding);
		if (!port.SetLineCoding(coding, detail)) {
			report.AddStep(name, StepStatus::Error, detail);
			return;
		}

		CompositeStatus status;
		StepStatus statusResult = ReadStatus(port, status, detail);
		if (statusResult == StepStatus::Error) {
			report.AddStep(name, StepStatus::Error, detail);
			return;
		}

		if (statusResult == StepStatus::Fail) {
			report.AddStep(name, StepStatus::Fail, detail);
			continue;
		}

		bool same = SameLineCoding(status.coding, coding);
		report.AddStep(name, same ? StepStatus::Pass : StepStatus::Fail, "the device reports " + DescribeLineCoding(status.coding));
		AddDtrStep(status, "DTR seen by the device at " + DescribeLineCoding(coding), report);
	}

	const std::string unknown = "nosuchcommand";
	std::vector<std::string> unknownBody;
	StepStatus exchange = Exchange(port, unknown, unknownBody, detail);
	if (exchange == StepStatus::Pass) {
		const std::string expected = "error: unknown command '" + unknown + "', try help";
		if (unknownBody.size() == 1 && unknownBody[0] == expected) {
			report.AddStep("unknown command", StepStatus::Pass, "the error line");
		} else {
			report.AddStep("unknown command", StepStatus::Fail, "the answer was '" + (unknownBody.empty() ? std::string() : unknownBody[0]) + "'");
		}
	} else {
		report.AddStep("unknown command", exchange, detail);
	}
}


// Sends the move and wheel commands, one at a time, and lets the events of each one arrive before the next. False when a command
// failed; the failure is added to the report.
bool SendMotionCommands(SerialPort& port, RawInputListener& listener, CheckReport& report) {
	const std::string motionCommands[4] = {"move 25 -10", "move -25 10", "wheel 1", "wheel -1"};
	listener.ClearEvents();
	for (const std::string& command : motionCommands) {
		std::vector<std::string> body;
		std::string detail;
		StepStatus exchange = Exchange(port, command, body, detail);
		if (exchange != StepStatus::Pass) {
			report.AddStep("motion commands", exchange, command + ": " + detail);
			return false;
		}

		if (body.size() != 1 || body[0] != "ok") {
			report.AddStep("motion commands", StepStatus::Fail, command + " answered '" + (body.empty() ? std::string() : body[0]) + "'");
			return false;
		}

		listener.Pump(MotionPumpMs);
	}

	report.AddStep("motion commands", StepStatus::Pass, "move and wheel answered ok");
	return true;
}


// The motion part of the hid check. The pointer is saved before the first command and put back after the last one. The guard covers
// every exit, and the restore is a step of its own, so a failed put-back is reported.
void RunMotion(SerialPort& port, RawInputListener& listener, CheckReport& report) {
	PointerPosition pointer;
	std::string detail;
	if (!pointer.Save(detail)) {
		report.AddStep("pointer position", StepStatus::Error, detail);
		return;
	}

	// The legs are the movements the commands send, in the order they are sent. The raw counts must net to zero, which shows that the
	// board's movement came back. The screen position is put back separately, because pointer acceleration does not undo a move exactly.
	const MouseEvent legs[4] = {
		{25, -10, 0},
		{-25, 10, 0},
		{0, 0, 1},
		{0, 0, -1}
	};
	if (SendMotionCommands(port, listener, report)) {
		std::vector<MouseEvent> legList(legs, legs + 4);
		bool motionOk = CheckMotionReturned(legList, listener.Mouse(), detail);
		report.AddStep("motion nets to zero", motionOk ? StepStatus::Pass : StepStatus::Fail, detail);
	}

	std::string restoreError;
	if (pointer.Restore(restoreError)) {
		report.AddStep("pointer put back", StepStatus::Pass, "where it was before the motion commands");
	} else {
		report.AddStep("pointer put back", StepStatus::Error, restoreError);
	}
}


void RunHid(const BoardInfo& board, CheckReport& report) {
	SerialPort port;
	if (!OpenCommandLine(board, port, report)) {
		return;
	}

	CompositeStatus status;
	std::string detail;
	StepStatus statusResult = ReadStatus(port, status, detail);
	if (statusResult != StepStatus::Pass) {
		report.AddStep("USB state", StepStatus::Error, detail);
		return;
	}

	// The keyboard and the mouse exist only once the host has configured the device.
	if (status.state != "Configured") {
		report.AddStep("USB state", StepStatus::Error, "the host has not configured the board (state " + status.state + ")");
		return;
	}

	report.AddStep("USB state", StepStatus::Pass, "Configured");
	RawInputListener listener(board.productId, board.serial);
	if (!listener.Start(detail)) {
		report.AddStep("Raw Input", StepStatus::Error, detail);
		return;
	}

	for (std::uint16_t index = 0; index < FunctionKeyCount; index++) {
		std::string command = "key " + FunctionKeyName(index);
		std::vector<std::string> body;
		StepStatus exchange = Exchange(port, command, body, detail);
		if (exchange != StepStatus::Pass) {
			report.AddStep("key commands", exchange, command + ": " + detail);
			return;
		}

		if (body.size() != 1 || body[0] != "ok") {
			report.AddStep("key commands", StepStatus::Fail, command + " answered '" + (body.empty() ? std::string() : body[0]) + "'");
			return;
		}

		listener.Pump(KeyPumpMs);
	}

	report.AddStep("key commands", StepStatus::Pass, "F13 to F24 answered ok");
	bool keysOk = CheckFunctionKeys(listener.Keys(), detail);
	report.AddStep("F13 to F24 from this board", keysOk ? StepStatus::Pass : StepStatus::Fail, detail);

	RunMotion(port, listener, report);
}


void RunVendor(const BoardInfo& board, CheckReport& report) {
	std::string path;
	std::string error;
	bool found = false;
	if (!FindVendorPath(board.serial, path, found, error)) {
		report.AddStep("vendor interface", StepStatus::Error, error);
		return;
	}

	if (!found) {
		report.AddStep("vendor interface", StepStatus::Error, "no WinUSB interface MI_04 for this board (the MS OS 2.0 descriptor did not bind)");
		return;
	}

	VendorChannel channel;
	if (!channel.Open(path, error)) {
		report.AddStep("vendor interface", StepStatus::Error, error);
		return;
	}

	report.AddStep("vendor interface", StepStatus::Pass, "WinUSB opened on MI_04");
	if (!channel.CheckEndpoints(error)) {
		report.AddStep("bulk endpoints", StepStatus::Fail, error);
		return;
	}

	report.AddStep("bulk endpoints", StepStatus::Pass, "IN 0x86, OUT 0x07");
	std::random_device entropy;
	for (std::uint32_t size : VendorBlockSizes) {
		std::string name = std::to_string(size) + " bytes loopback";
		std::vector<std::uint8_t> sent(size);
		FillPattern(sent, entropy());
		std::vector<std::uint8_t> received;

		std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		if (!channel.RoundTrip(sent, received, error)) {
			report.AddStep(name, StepStatus::Error, error);
			return;
		}

		std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
		double seconds = elapsed.count() > 0.0 ? elapsed.count() : 1e-9;
		std::string detail;
		std::size_t offset = 0;
		if (FirstMismatch(sent, received, offset)) {
			detail = "the bytes differ, first at offset " + std::to_string(offset);
			report.AddStep(name, StepStatus::Fail, detail);
			continue;
		}

		AppendFormat(detail, "%.3f MB/s", static_cast<double>(size) / 1000000.0 / seconds);
		report.AddStep(name, StepStatus::Pass, detail);
	}
}

}


void RunCompositeChecks(CompositeCheck which, const BoardInfo& board, std::vector<CheckReport>& reports) {
	if (which == CompositeCheck::All || which == CompositeCheck::Cdc) {
		CheckReport report("cdc");
		RunCdc(board, report);
		reports.push_back(report);
	}

	if (which == CompositeCheck::All || which == CompositeCheck::Hid) {
		CheckReport report("hid");
		RunHid(board, report);
		reports.push_back(report);
	}

	if (which == CompositeCheck::All || which == CompositeCheck::Vendor) {
		CheckReport report("vendor");
		RunVendor(board, report);
		reports.push_back(report);
	}
}
