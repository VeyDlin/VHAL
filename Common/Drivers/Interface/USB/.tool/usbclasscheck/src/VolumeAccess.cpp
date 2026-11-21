#include "VolumeAccess.h"

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

#include <windows.h>
#include <winioctl.h>

#include "Text.h"
#include "WinText.h"

namespace {

// The read size of the uncached read-back. A multiple of the sector size, and the buffer is page aligned.
constexpr std::size_t ReadChunkBytes = 64 * 1024;

// The storage descriptor reply. Strings follow the structure at offsets the descriptor gives.
constexpr std::size_t DescriptorBufferBytes = 1024;


// Reads one NUL terminated string of the descriptor, bounded by the reply size.
std::string DescriptorString(const std::vector<std::uint8_t>& buffer, DWORD offset) {
	if (offset == 0 || offset >= buffer.size()) {
		return "";
	}

	std::string text;
	for (std::size_t index = offset; index < buffer.size() && buffer[index] != 0; index++) {
		text.push_back(static_cast<char>(buffer[index]));
	}

	return text;
}


// Closes a file that this run created and deletes it. The error text gets a note when the delete fails too, so the file left behind is
// reported.
void AbandonNewFile(HANDLE file, const std::wstring& widePath, std::string& error) {
	CloseHandle(file);
	if (!DeleteFileW(widePath.c_str())) {
		error += " (the partial file could not be deleted either: " + FormatWindowsError(GetLastError()) + ")";
	}
}


// The drive path a device open needs: \\.\E: for the root E:\.
std::wstring DevicePathOf(const std::string& root) {
	std::string trimmed = root;
	if (!trimmed.empty() && trimmed.back() == '\\') {
		trimmed.pop_back();
	}

	return L"\\\\.\\" + WidenAscii(trimmed);
}

}


bool FindVolumesLabelled(const std::string& label, std::vector<std::string>& roots, std::string& error) {
	roots.clear();
	DWORD length = GetLogicalDriveStringsW(0, nullptr);
	if (length == 0) {
		error = "GetLogicalDriveStrings failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	std::vector<wchar_t> drives(length + 1, L'\0');
	if (GetLogicalDriveStringsW(length, drives.data()) == 0) {
		error = "GetLogicalDriveStrings failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	// A drive with no media (an empty card reader, say) makes the label query fail, and Windows would show a "no disk in the
	// drive" dialog for it. The errors are turned off for the scan; the drive is simply skipped.
	UINT previousErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);
	for (const wchar_t* drive = drives.data(); *drive != L'\0'; drive += wcslen(drive) + 1) {
		// Only removable and fixed drives can be the board's disk. A network, optical or RAM drive is skipped before its label is read.
		UINT driveType = GetDriveTypeW(drive);
		if (driveType != DRIVE_REMOVABLE && driveType != DRIVE_FIXED) {
			continue;
		}

		wchar_t volumeLabel[MAX_PATH + 1] {};
		if (!GetVolumeInformationW(drive, volumeLabel, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0)) {
			continue;
		}

		// The label is compared as UTF-8, so a label with letters outside ASCII matches too. The case is folded for ASCII only, which is what
		// the board's label needs.
		if (EqualsIgnoreCase(NarrowUtf8(std::wstring(volumeLabel)), label)) {
			roots.push_back(NarrowAscii(drive));
		}
	}

	SetErrorMode(previousErrorMode);
	return true;
}


bool ReadDiskIdentity(const std::string& root, DiskIdentity& identity, std::string& error) {
	std::wstring path = DevicePathOf(root);
	HANDLE drive = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (drive == INVALID_HANDLE_VALUE) {
		error = "cannot open " + root + " for its disk identity: " + FormatWindowsError(GetLastError());
		return false;
	}

	STORAGE_PROPERTY_QUERY query {};
	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;
	std::vector<std::uint8_t> reply(DescriptorBufferBytes, 0);
	DWORD returned = 0;
	BOOL ok = DeviceIoControl(
		drive,
		IOCTL_STORAGE_QUERY_PROPERTY,
		&query,
		sizeof(query),
		reply.data(),
		static_cast<DWORD>(reply.size()),
		&returned,
		nullptr);
	CloseHandle(drive);
	if (!ok || returned < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
		error = "the disk identity of " + root + " could not be read: " + FormatWindowsError(GetLastError());
		return false;
	}

	const STORAGE_DEVICE_DESCRIPTOR* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(reply.data());
	identity.vendor = TrimSpaces(DescriptorString(reply, descriptor->VendorIdOffset));
	identity.product = TrimSpaces(DescriptorString(reply, descriptor->ProductIdOffset));
	identity.serial = TrimSpaces(DescriptorString(reply, descriptor->SerialNumberOffset));
	return true;
}


bool DiskNumberOfVolume(const std::string& root, DiskNumber& number, std::string& error) {
	return DiskNumberOfDevice(NarrowAscii(DevicePathOf(root)), number, error);
}


bool DiskNumberOfDevice(const std::string& devicePath, DiskNumber& number, std::string& error) {
	HANDLE device = CreateFileW(WidenAscii(devicePath).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (device == INVALID_HANDLE_VALUE) {
		error = "cannot open " + devicePath + " for its disk number: " + FormatWindowsError(GetLastError());
		return false;
	}

	STORAGE_DEVICE_NUMBER reply {};
	DWORD returned = 0;
	BOOL ok = DeviceIoControl(device, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &reply, sizeof(reply), &returned, nullptr);
	CloseHandle(device);
	if (!ok || returned < sizeof(STORAGE_DEVICE_NUMBER)) {
		error = "the disk number of " + devicePath + " could not be read: " + FormatWindowsError(GetLastError());
		return false;
	}

	number.deviceType = reply.DeviceType;
	number.deviceNumber = reply.DeviceNumber;
	return true;
}


bool FreeSpaceOf(const std::string& root, std::uint64_t& freeBytes, std::uint64_t& sectorBytes, std::string& error) {
	ULARGE_INTEGER available {};
	ULARGE_INTEGER total {};
	ULARGE_INTEGER totalFree {};
	if (!GetDiskFreeSpaceExA(root.c_str(), &available, &total, &totalFree)) {
		error = "GetDiskFreeSpaceEx failed for " + root + ": " + FormatWindowsError(GetLastError());
		return false;
	}

	DWORD sectorsPerCluster = 0;
	DWORD bytesPerSector = 0;
	DWORD freeClusters = 0;
	DWORD totalClusters = 0;
	if (!GetDiskFreeSpaceW(WidenAscii(root).c_str(), &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters)) {
		error = "GetDiskFreeSpace failed for " + root + ": " + FormatWindowsError(GetLastError());
		return false;
	}

	freeBytes = available.QuadPart;
	sectorBytes = bytesPerSector;
	return true;
}


bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error) {
	std::wstring widePath = WidenUtf8(path);
	HANDLE file = CreateFileW(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		error = "cannot open " + path + ": " + FormatWindowsError(GetLastError());
		return false;
	}

	bytes.clear();
	std::uint8_t buffer[4096];
	while (true) {
		DWORD count = 0;
		if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) {
			error = "cannot read " + path + ": " + FormatWindowsError(GetLastError());
			CloseHandle(file);
			return false;
		}

		if (count == 0) {
			break;
		}

		bytes.insert(bytes.end(), buffer, buffer + count);
	}

	CloseHandle(file);
	return true;
}


bool CreateNewFile(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
	std::wstring widePath = WidenUtf8(path);
	// CREATE_NEW: a file that already exists is not overwritten, because it may be someone's own file.
	HANDLE file = CreateFileW(widePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		error = "cannot create " + path + ": " + FormatWindowsError(GetLastError());
		return false;
	}

	// From here the file exists and this call created it. A failure below deletes it again.
	std::size_t written = 0;
	while (written < bytes.size()) {
		DWORD count = 0;
		DWORD chunk = static_cast<DWORD>(bytes.size() - written);
		if (!WriteFile(file, bytes.data() + written, chunk, &count, nullptr) || count == 0) {
			error = "cannot write " + path + ": " + FormatWindowsError(GetLastError());
			AbandonNewFile(file, widePath, error);
			return false;
		}

		written += count;
	}

	if (!FlushFileBuffers(file)) {
		error = "cannot flush " + path + ": " + FormatWindowsError(GetLastError());
		AbandonNewFile(file, widePath, error);
		return false;
	}

	CloseHandle(file);
	return true;
}


bool ReadBackUncached(const std::string& path, std::size_t size, std::vector<std::uint8_t>& bytes, std::string& error) {
	std::wstring widePath = WidenUtf8(path);
	HANDLE file = CreateFileW(
		widePath.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		error = "cannot open " + path + " for an uncached read: " + FormatWindowsError(GetLastError());
		return false;
	}

	// VirtualAlloc returns page aligned memory, which satisfies the sector alignment of unbuffered reads.
	void* buffer = VirtualAlloc(nullptr, ReadChunkBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (buffer == nullptr) {
		error = "VirtualAlloc failed: " + FormatWindowsError(GetLastError());
		CloseHandle(file);
		return false;
	}

	bytes.clear();
	bool ok = true;
	while (bytes.size() < size) {
		std::size_t remaining = size - bytes.size();
		DWORD request = static_cast<DWORD>(remaining < ReadChunkBytes ? remaining : ReadChunkBytes);
		DWORD count = 0;
		if (!ReadFile(file, buffer, request, &count, nullptr)) {
			error = "uncached read of " + path + " failed: " + FormatWindowsError(GetLastError());
			ok = false;
			break;
		}

		if (count == 0) {
			error = "the file ended after " + std::to_string(bytes.size()) + " of " + std::to_string(size) + " bytes";
			ok = false;
			break;
		}

		const std::uint8_t* data = static_cast<const std::uint8_t*>(buffer);
		bytes.insert(bytes.end(), data, data + count);
	}

	VirtualFree(buffer, 0, MEM_RELEASE);
	CloseHandle(file);
	return ok;
}


bool PathExists(const std::string& path) {
	std::wstring widePath = WidenUtf8(path);
	if (GetFileAttributesW(widePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
		return true;
	}

	// Only a path that is not there counts as absent. Any other failure counts as present, so a file that may exist is never taken for
	// a free name.
	DWORD code = GetLastError();
	return code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND;
}


bool DeleteFileAt(const std::string& path, std::string& error) {
	std::wstring widePath = WidenUtf8(path);
	if (!DeleteFileW(widePath.c_str())) {
		error = "cannot delete " + path + ": " + FormatWindowsError(GetLastError());
		return false;
	}

	return true;
}
