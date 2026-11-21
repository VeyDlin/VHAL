#include "SerialPort.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "StatusAnswer.h"
#include "WinText.h"

namespace {

// Each read returns after this many milliseconds without data, so a deadline is checked at least this often.
constexpr DWORD ReadPollMs = 20;
constexpr DWORD WriteTimeoutMs = 1000;


BYTE ParityCode(const std::string& parity) {
	if (parity == "odd") {
		return ODDPARITY;
	}

	if (parity == "even") {
		return EVENPARITY;
	}

	if (parity == "mark") {
		return MARKPARITY;
	}

	if (parity == "space") {
		return SPACEPARITY;
	}

	return NOPARITY;
}


BYTE StopBitsCode(const std::string& stopBits) {
	if (stopBits == "1.5") {
		return ONE5STOPBITS;
	}

	if (stopBits == "2") {
		return TWOSTOPBITS;
	}

	return ONESTOPBIT;
}

}


SerialPort::~SerialPort() {
	Close();
}


bool SerialPort::Open(const std::string& portName, std::string& error) {
	Close();
	dtrOn = false;
	// The \\.\ prefix is needed for every port, and COM10 and up do not work without it.
	std::wstring path = L"\\\\.\\" + WidenAscii(portName);
	handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		error = "cannot open " + portName + ": " + FormatWindowsError(GetLastError());
		return false;
	}

	if (!SetupComm(handle, 4096, 4096)) {
		error = "SetupComm failed: " + FormatWindowsError(GetLastError());
		Close();
		return false;
	}

	COMMTIMEOUTS timeouts {};
	timeouts.ReadIntervalTimeout = MAXDWORD;
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.ReadTotalTimeoutConstant = ReadPollMs;
	timeouts.WriteTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = WriteTimeoutMs;
	if (!SetCommTimeouts(handle, &timeouts)) {
		error = "SetCommTimeouts failed: " + FormatWindowsError(GetLastError());
		Close();
		return false;
	}

	return true;
}


void SerialPort::Close() {
	if (handle != INVALID_HANDLE_VALUE) {
		CloseHandle(handle);
		handle = INVALID_HANDLE_VALUE;
	}
}


bool SerialPort::IsOpen() const {
	return handle != INVALID_HANDLE_VALUE;
}


bool SerialPort::SetLineCoding(const LineCoding& coding, std::string& error) {
	DCB dcb {};
	dcb.DCBlength = sizeof(dcb);
	if (!GetCommState(handle, &dcb)) {
		error = "GetCommState failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	dcb.BaudRate = coding.baud;
	dcb.ByteSize = static_cast<BYTE>(coding.dataBits);
	dcb.Parity = ParityCode(coding.parity);
	dcb.StopBits = StopBitsCode(coding.stopBits);
	dcb.fBinary = TRUE;
	dcb.fParity = dcb.Parity != NOPARITY ? TRUE : FALSE;
	// GetCommState() does not report a DTR raised with EscapeCommFunction(), so without this the new state would drop it.
	dcb.fDtrControl = dtrOn ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
	if (!SetCommState(handle, &dcb)) {
		error = "SetCommState failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	return true;
}


bool SerialPort::SetDtr(bool on, std::string& error) {
	if (!EscapeCommFunction(handle, on ? SETDTR : CLRDTR)) {
		error = "EscapeCommFunction failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	dtrOn = on;
	return true;
}


void SerialPort::DiscardInput() {
	PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
}


bool SerialPort::Write(const std::string& text, std::string& error) {
	std::size_t sent = 0;
	while (sent < text.size()) {
		DWORD written = 0;
		if (!WriteFile(handle, text.data() + sent, static_cast<DWORD>(text.size() - sent), &written, nullptr)) {
			error = "write failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		if (written == 0) {
			error = "write made no progress";
			return false;
		}

		sent += written;
	}

	return true;
}


bool SerialPort::ReadAnswer(std::string& answer, std::uint32_t timeoutMs, std::string& error) {
	ULONGLONG deadline = GetTickCount64() + timeoutMs;
	while (!AnswerComplete(answer)) {
		if (GetTickCount64() >= deadline) {
			error = "no complete answer within " + std::to_string(timeoutMs) + " ms (received '" + answer + "')";
			return false;
		}

		if (!ReadFor(answer, ReadPollMs, error)) {
			return false;
		}
	}

	return true;
}


bool SerialPort::ReadFor(std::string& received, std::uint32_t timeoutMs, std::string& error) {
	ULONGLONG deadline = GetTickCount64() + timeoutMs;
	char buffer[256];
	do {
		DWORD count = 0;
		if (!ReadFile(handle, buffer, sizeof(buffer), &count, nullptr)) {
			error = "read failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		received.append(buffer, count);
	} while (GetTickCount64() < deadline && received.size() < 1024 * 1024);

	return true;
}
