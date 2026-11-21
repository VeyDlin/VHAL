#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The mounted volumes whose label matches (ignoring case), as roots such as "E:\".
bool FindVolumesLabelled(const std::string& label, std::vector<std::string>& roots, std::string& error);

// The vendor, product and serial number that the disk of a volume reports (STORAGE_DEVICE_DESCRIPTOR).
struct DiskIdentity {
	std::string vendor;
	std::string product;
	std::string serial;
};

bool ReadDiskIdentity(const std::string& root, DiskIdentity& identity, std::string& error);

// The disk a volume or a disk interface belongs to, as IOCTL_STORAGE_GET_DEVICE_NUMBER reports it: the device type (FILE_DEVICE_DISK
// for a disk) and the disk's number. A volume and the disk it is on give the same number.
struct DiskNumber {
	std::uint32_t deviceType = 0;
	std::uint32_t deviceNumber = 0;
};

// The disk number of the volume at root, such as "E:\".
bool DiskNumberOfVolume(const std::string& root, DiskNumber& number, std::string& error);

// The disk number of a device path: a volume (\\.\E:) or a disk interface path (\\?\...). Opened without access, so no rights are needed.
bool DiskNumberOfDevice(const std::string& devicePath, DiskNumber& number, std::string& error);

// The free bytes available to the caller and the sector size of the volume at root.
bool FreeSpaceOf(const std::string& root, std::uint64_t& freeBytes, std::uint64_t& sectorBytes, std::string& error);

// Reads a whole small file through the file system (the buffers of the cache included).
bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error);

// Creates a file that does not exist yet, writes all of bytes, and flushes them to the device before it returns. A file that already
// exists is not touched. If the write or the flush fails after the file was created, the file is deleted again, so no partial file is
// left by this call.
bool CreateNewFile(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error);

// Reads size bytes back with FILE_FLAG_NO_BUFFERING, so the data comes from the device and not from the cache. size must be
// a whole number of sectors.
bool ReadBackUncached(const std::string& path, std::size_t size, std::vector<std::uint8_t>& bytes, std::string& error);

// True when something exists at the path. A path that cannot be checked for any reason other than "not found" counts as present.
bool PathExists(const std::string& path);

bool DeleteFileAt(const std::string& path, std::string& error);
