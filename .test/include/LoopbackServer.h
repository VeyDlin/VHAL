#pragma once
// A loopback server that a host test runs as a child process. The server is a Python script that writes the port it bound as
// the first line of its standard output. The test redirects that output to a port file in the working folder, and gives the
// script a pipe as its standard input. The script reads its standard input to the end and then exits, so closing the pipe stops
// it. Stop() closes the pipe, and a test that ends in any other way (a failed check, a crash) closes it as well, because the
// pipe belongs to the test process.
//
// FindPython() returns the Python 3 command, and RunSilently() runs a probe such as an import, so a test can skip when the
// interpreter or a package is missing. Nothing is installed.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>


namespace VhalLoopback {


#if defined(_WIN32)
inline constexpr const char* nullDevice = "NUL";


inline FILE* OpenPipe(const std::string& command) {
	return _popen(command.c_str(), "w");
}


inline void ClosePipe(FILE* pipe) {
	_pclose(pipe);
}


inline std::vector<std::string> PythonCandidates() {
	return { "python", "python3", "py -3" };
}


// cmd /c strips one pair of quotes when the command starts with one, so the whole command is quoted.
inline std::string ShellCommand(const std::string& command) {
	return "\"" + command + "\"";
}
#else
inline constexpr const char* nullDevice = "/dev/null";


inline FILE* OpenPipe(const std::string& command) {
	return popen(command.c_str(), "w");
}


inline void ClosePipe(FILE* pipe) {
	pclose(pipe);
}


inline std::vector<std::string> PythonCandidates() {
	return { "python3", "python" };
}


inline std::string ShellCommand(const std::string& command) {
	return command;
}
#endif


// True when the command exits with status 0. Its output goes to the null device.
inline bool RunSilently(const std::string& command) {
	std::string redirected = command + " > " + nullDevice + " 2>&1";
	return std::system(ShellCommand(redirected).c_str()) == 0;
}


// The command that starts Python 3, or an empty string when no Python 3 is found.
inline std::string FindPython() {
	const std::vector<std::string> candidates = PythonCandidates();
	for (const std::string& candidate : candidates) {
		if (RunSilently(candidate + " -c \"import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)\"")) {
			return candidate;
		}
	}

	return std::string();
}


// Reads the port from a port file. The file holds one decimal number and a line end, which the script writes in one piece: "\n",
// or "\r\n" where the text mode of the platform turns "\n" into "\r\n". Returns 0 while the file is missing, incomplete or not a
// port.
inline std::uint16_t ReadPort(const std::string& portFile) {
	std::FILE* file = std::fopen(portFile.c_str(), "rb");
	if (file == nullptr) {
		return 0;
	}

	std::string text;
	char buffer[64];
	std::size_t count = 0;
	while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
		text.append(buffer, count);
	}

	std::fclose(file);

	std::size_t lineEnd = text.find('\n');
	if (lineEnd == std::string::npos) {
		return 0;
	}

	std::size_t digitsEnd = (lineEnd > 0 && text[lineEnd - 1] == '\r') ? lineEnd - 1 : lineEnd;
	if (digitsEnd == 0) {
		return 0;
	}

	unsigned long value = 0;
	for (std::size_t index = 0; index < digitsEnd; index++) {
		char digit = text[index];
		if (digit < '0' || digit > '9') {
			return 0;
		}

		value = value * 10 + static_cast<unsigned long>(digit - '0');
		if (value > 65535) {
			return 0;
		}
	}

	return static_cast<std::uint16_t>(value);
}


// One running server. Start() launches it, Stop() ends it; the destructor calls Stop().
class Server {
public:
	Server() = default;
	Server(const Server&) = delete;
	Server& operator=(const Server&) = delete;


	~Server() {
		Stop();
	}


	// Starts "<python> <script> <arguments>" and returns the port the script reports, or 0 when none was reported in about thirty
	// seconds. The port and log files are name.port and name.log in the working folder.
	std::uint16_t Start(
		const std::string& python,
		const std::filesystem::path& script,
		const std::string& arguments,
		const std::string& name) {
		Stop();

		std::string portFile = name + ".port";
		std::string logFile = name + ".log";
		std::error_code ignored;
		std::filesystem::remove(portFile, ignored);

		std::string command = python + " \"" + script.string() + "\" " + arguments + " > \"" + portFile + "\" 2> \"" + logFile + "\"";
		pipe = OpenPipe(ShellCommand(command));
		if (pipe == nullptr) {
			return 0;
		}

		for (int attempt = 0; attempt < 600; attempt++) {
			std::uint16_t port = ReadPort(portFile);
			if (port != 0) {
				return port;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		return 0;
	}


	// Closes the pipe, which ends the script, and waits until it has exited.
	void Stop() {
		if (pipe != nullptr) {
			ClosePipe(pipe);
			pipe = nullptr;
		}
	}


private:
	FILE* pipe = nullptr;
};


}
