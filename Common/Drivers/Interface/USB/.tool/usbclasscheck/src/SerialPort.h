#pragma once

#include <cstdint>
#include <string>

#include <windows.h>

#include "StatusAnswer.h"

// A COM port of a CDC-ACM device: the line coding is set with SetCommState, DTR with EscapeCommFunction. Reads return after a
// short timeout, so a caller polls them with its own deadline.
class SerialPort {
private:
	HANDLE handle = INVALID_HANDLE_VALUE;
	// The DTR state SetDtr() last set. SetCommState() applies the DCB's fDtrControl, so SetLineCoding() writes this state into it.
	bool dtrOn = false;


public:
	SerialPort() = default;

	SerialPort(const SerialPort&) = delete;
	SerialPort& operator=(const SerialPort&) = delete;

	~SerialPort();

	bool Open(const std::string& portName, std::string& error);

	void Close();

	bool IsOpen() const;

	bool SetLineCoding(const LineCoding& coding, std::string& error);

	bool SetDtr(bool on, std::string& error);

	// Drops the bytes the device sent before the check started, so an old prompt is not taken for the answer.
	void DiscardInput();

	bool Write(const std::string& text, std::string& error);

	// Reads until the answer is complete (see AnswerComplete) or the time is up. False with an error when it times out.
	bool ReadAnswer(std::string& answer, std::uint32_t timeoutMs, std::string& error);

	// Reads what arrives within about the time given and appends it to received. Used by the console capture.
	bool ReadFor(std::string& received, std::uint32_t timeoutMs, std::string& error);
};
