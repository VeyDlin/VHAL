#include "ConsoleCapture.h"

#include <cstdint>
#include <string>

#include "StatusAnswer.h"

namespace {

// The console runs at this rate, 8N1 with no parity and one stop bit, as the demo's USART1 is set.
const LineCoding ConsoleLineCoding = {115200, 8, "none", "1"};

constexpr std::uint32_t ConsolePollMs = 50;

}


ConsoleCapture::~ConsoleCapture() {
	if (worker.joinable()) {
		Stop();
	}
}


bool ConsoleCapture::Start(const std::string& portName, std::string& error) {
	if (!port.Open(portName, error)) {
		return false;
	}

	// A terminal raises DTR when it opens a port, and the console is read the same way. SetDtr goes first, so the line coding that follows
	// keeps DTR raised (SetLineCoding writes the state SetDtr last set).
	if (!port.SetDtr(true, error)) {
		port.Close();
		return false;
	}

	if (!port.SetLineCoding(ConsoleLineCoding, error)) {
		port.Close();
		return false;
	}

	port.DiscardInput();
	stopRequested = false;
	captured.clear();
	worker = std::thread(&ConsoleCapture::Run, this);
	return true;
}


std::string ConsoleCapture::Stop() {
	stopRequested = true;
	if (worker.joinable()) {
		worker.join();
	}

	port.Close();
	return captured;
}


void ConsoleCapture::Run() {
	while (!stopRequested) {
		std::string error;
		// A read error ends the capture; what was read so far is still returned.
		if (!port.ReadFor(captured, ConsolePollMs, error)) {
			return;
		}
	}
}
