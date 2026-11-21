#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "SerialPort.h"

// Reads a board's console on its own thread while a check runs, and keeps what it printed. The console is USART1 at 115200
// 8N1 on the board; the reader keeps the port open until Stop().
class ConsoleCapture {
private:
	SerialPort port;
	std::atomic<bool> stopRequested {false};
	std::thread worker;
	std::string captured;


public:
	ConsoleCapture() = default;

	ConsoleCapture(const ConsoleCapture&) = delete;
	ConsoleCapture& operator=(const ConsoleCapture&) = delete;

	~ConsoleCapture();

	bool Start(const std::string& portName, std::string& error);

	// Stops the reader, waits for it, closes the port and returns everything the console printed.
	std::string Stop();


private:
	void Run();
};
