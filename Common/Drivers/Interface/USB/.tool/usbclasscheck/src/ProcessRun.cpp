#include "ProcessRun.h"

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "CheckData.h"
#include "ProcessPoll.h"
#include "WinText.h"

namespace {

// The longest environment value Windows returns, in characters. The PATH and the resolved program path are read into buffers this size.
constexpr DWORD EnvironmentBufferChars = 32768;

// Each wait for the program's end lasts this long, so the output is read again soon after it arrives.
constexpr DWORD PollMs = 10;


// Owns a handle that is closed when it goes out of scope. An invalid value is never closed.
class ScopedHandle {
private:
	HANDLE handle = nullptr;


public:
	explicit ScopedHandle(HANDLE value) {
		handle = value;
	}


	ScopedHandle(const ScopedHandle&) = delete;
	ScopedHandle& operator=(const ScopedHandle&) = delete;


	~ScopedHandle() {
		if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
			CloseHandle(handle);
		}
	}


	HANDLE Get() const {
		return handle;
	}


	void Reset() {
		if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
			CloseHandle(handle);
		}

		handle = nullptr;
	}
};


// True when the program name has a folder part: a separator or a drive colon. Such a name is a path, and it is not searched for.
bool HasFolderPart(const std::string& program) {
	return program.find_first_of("\\/:") != std::string::npos;
}


// Finds the full path of the program that CreateProcess is given. A name with a folder part is made absolute as it is. A bare name is
// searched for in the PATH directories only: SearchPathW gets the PATH value, so the current folder is never searched, and a program
// of the same name in the folder the tool runs from is not started by accident.
bool ResolveProgram(const std::string& program, std::wstring& fullPath, std::string& error) {
	std::wstring wideName = WidenUtf8(program);
	std::vector<wchar_t> resolved(EnvironmentBufferChars, L'\0');
	if (HasFolderPart(program)) {
		DWORD length = GetFullPathNameW(wideName.c_str(), EnvironmentBufferChars, resolved.data(), nullptr);
		if (length == 0 || length >= EnvironmentBufferChars) {
			error = "the path cannot be resolved: " + FormatWindowsError(GetLastError());
			return false;
		}

		fullPath = resolved.data();
		return true;
	}

	std::vector<wchar_t> pathValue(EnvironmentBufferChars, L'\0');
	DWORD pathLength = GetEnvironmentVariableW(L"PATH", pathValue.data(), EnvironmentBufferChars);
	if (pathLength == 0 || pathLength >= EnvironmentBufferChars) {
		error = "not found, because the PATH is not set";
		return false;
	}

	DWORD length = SearchPathW(pathValue.data(), wideName.c_str(), L".exe", EnvironmentBufferChars, resolved.data(), nullptr);
	if (length == 0 || length >= EnvironmentBufferChars) {
		error = "not found on the PATH";
		return false;
	}

	fullPath = resolved.data();
	return true;
}

}


bool RunProgram(
	const std::string& program,
	const std::vector<std::string>& arguments,
	std::uint32_t timeoutMs,
	int& exitCode,
	std::string& output,
	std::string& error)
{
	output.clear();
	std::wstring fullPath;
	if (!ResolveProgram(program, fullPath, error)) {
		return false;
	}

	SECURITY_ATTRIBUTES security {};
	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
		error = "CreatePipe failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	ScopedHandle reader(readPipe);
	ScopedHandle writer(writePipe);
	// The reader stays in this process, so it must not be inherited by the child.
	if (!SetHandleInformation(reader.Get(), HANDLE_FLAG_INHERIT, 0)) {
		error = "SetHandleInformation failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = writer.Get();
	startup.hStdError = writer.Get();

	// The program is named by its full path, which is also its first argument. The arguments are quoted the way CommandLineOf gives them.
	std::wstring wideLine = L"\"" + fullPath + L"\"";
	std::string argumentText = CommandLineOf(arguments);
	if (!argumentText.empty()) {
		wideLine += L" " + WidenUtf8(argumentText);
	}

	std::vector<wchar_t> mutableLine(wideLine.begin(), wideLine.end());
	mutableLine.push_back(L'\0');

	PROCESS_INFORMATION process {};
	if (!CreateProcessW(fullPath.c_str(), mutableLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
		error = "cannot start " + program + ": " + FormatWindowsError(GetLastError());
		return false;
	}

	ScopedHandle processHandle(process.hProcess);
	ScopedHandle threadHandle(process.hThread);
	// The writer must be closed here, or the reader never sees the end of the output.
	writer.Reset();

	// The output is read while the program runs. A program that writes more than the pipe holds waits for the read, so waiting for its
	// end before reading would block both of them. The time is still limited by timeoutMs.
	ULONGLONG deadline = GetTickCount64() + timeoutMs;
	std::vector<char> buffer(4096);
	bool outputOpen = true;
	while (true) {
		// The exit is read before the pipe. A program that has ended has written all its output, so the pipe is read to its end below.
		bool exited = WaitForSingleObject(processHandle.Get(), 0) == WAIT_OBJECT_0;
		DWORD available = 0;
		if (outputOpen && !PeekNamedPipe(reader.Get(), nullptr, 0, nullptr, &available, nullptr)) {
			// The program has closed its end of the pipe, so no more output comes.
			outputOpen = false;
			available = 0;
		}

		// The deadline is checked on every pass, including the passes that read output, so a program that keeps writing is still ended.
		PollStep step = DecidePollStep(exited, available > 0, GetTickCount64() >= deadline);
		if (step == PollStep::Finish) {
			break;
		}

		if (step == PollStep::Timeout) {
			TerminateProcess(processHandle.Get(), 1);
			WaitForSingleObject(processHandle.Get(), 5000);
			error = "the program did not end within " + std::to_string(timeoutMs) + " ms and was stopped";
			return false;
		}

		if (step == PollStep::ReadOutput) {
			DWORD request = available < static_cast<DWORD>(buffer.size()) ? available : static_cast<DWORD>(buffer.size());
			DWORD count = 0;
			if (!ReadFile(reader.Get(), buffer.data(), request, &count, nullptr) || count == 0) {
				outputOpen = false;
			} else {
				output.append(buffer.data(), count);
			}
		} else {
			WaitForSingleObject(processHandle.Get(), PollMs);
		}
	}

	DWORD code = 0;
	if (!GetExitCodeProcess(processHandle.Get(), &code)) {
		error = "GetExitCodeProcess failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	exitCode = static_cast<int>(code);
	return true;
}
