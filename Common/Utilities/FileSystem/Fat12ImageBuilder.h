#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/CompileTime/TextLiteral.h>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>


// A FAT12 volume built at compile time, for a RAM disk that mounts without formatting. The boot sector has a BPB for 512-byte
// sectors, one or two FAT copies, a root directory with a volume label entry, and the files given to the builder. Windows, Linux
// and macOS mount such a volume as it is: the BPB, the FAT chains and the 8.3 directory entries are all written here.
//
// Usage, as for USBDescriptors: the chain is a captureless lambda, and Fat12Image gives the bytes in an array of exact size.
//
//   using Image = Fat12Image<+[] {
//       return Fat12ImageBuilder()
//           .Sectors(128)
//           .VolumeLabel("VHAL DISK")
//           .File("readme.txt", "Hello from the RAM disk.\r\n");
//   }>;
//
//   RamBlockDevice<512, 128> disk(Image::Bytes);
//
// Rules, checked at compile time (a violation is an error with its message):
//   - Sectors() is required, at most Fat12MaxSectors, and the FAT must fit a 16-bit sector count.
//   - Names are 8.3: a base of 1 to 8 characters, an extension of at most 3, at most one dot. Lower-case letters are upper-cased.
//     A character outside the FAT short-name set is an error.
//   - The volume has fewer than 4085 clusters, which is what makes it FAT12.
//   - The files and the label fit in the root directory and in the data area, and no two files have the same short name.
//
// The builder keeps its state in a fixed array of Fat12MaxBytes, and Fat12Image copies the result into an array of its exact size,
// so the capacity bounds the chain and the image in flash is no larger than the volume.


// The limits of the builder. The volume is at most Fat12MaxSectors sectors, and the builder holds Fat12MaxFiles files.
constexpr std::size_t Fat12SectorBytes = 512;
constexpr std::size_t Fat12MaxSectors = 512;
constexpr std::size_t Fat12MaxBytes = Fat12MaxSectors * Fat12SectorBytes;
constexpr std::size_t Fat12MaxFiles = 8;
// FAT12 cluster numbers run from 2, and the entries 0xFF0 to 0xFFF are reserved for marks, so a volume has fewer than 4085 clusters.
constexpr std::size_t Fat12MaxClusters = 4084;
constexpr uint8 Fat12MediaFixed = 0xF8;
constexpr uint16 Fat12EndOfChain = 0xFFF;
constexpr uint8 Fat12AttrArchive = 0x20;
constexpr uint8 Fat12AttrLabel = 0x08;
// 1980-01-01 in the DOS date format, used for every date field, so the image is the same on every build.
constexpr uint16 Fat12DosDate = 0x0021;


// Declared, never defined. A call from a constant expression is ill-formed, and the compiler prints the message in the template argument.
template<TextLiteral Message>
void Fat12Error();


// The rule check of the builder. Calls Fat12Error<Message>() when the condition does not hold.
template<TextLiteral Message>
constexpr void Fat12Require(bool condition) {
	if (!condition) {
		Fat12Error<Message>();
	}
}


// One file of the builder. The text or the bytes are kept as views, so the data must be a string literal or static storage.
struct Fat12File {
	std::array<char, 11> shortName{};
	std::string_view text;
	std::span<const uint8> bytes;
	bool isBytes = false;
	std::size_t size = 0;
};


// The bytes of a built volume. size is the number of bytes in use; the rest of the array is zero.
struct Fat12Volume {
	std::array<uint8, Fat12MaxBytes> bytes{};
	std::size_t size = 0;
};


constexpr char Fat12Upper(char value) {
	return (value >= 'a' && value <= 'z') ? static_cast<char>(value - 'a' + 'A') : value;
}


// The characters a FAT short name may hold: upper-case letters, digits and this set of symbols. Anything else, including a space
// inside the name and the lower-case letters after upper-casing, is rejected.
constexpr bool Fat12IsShortNameChar(char value) {
	if ((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9')) {
		return true;
	}

	for (char allowed : std::string_view("!#$%&'()-@^_`{}~")) {
		if (value == allowed) {
			return true;
		}
	}

	return false;
}


// Upper-cases one character of a name and checks it against the short-name set.
constexpr char Fat12CheckedChar(char value) {
	char upper = Fat12Upper(value);
	Fat12Require<"a short name character is not allowed">(Fat12IsShortNameChar(upper));
	return upper;
}


// The 11 bytes of a file's 8.3 name, upper-cased and space-padded.
constexpr std::array<char, 11> Fat12FileName(std::string_view name) {
	std::array<char, 11> result{};
	for (char& value : result) {
		value = ' ';
	}

	std::size_t dot = name.find('.');
	if (dot != std::string_view::npos) {
		Fat12Require<"a file name has at most one dot">(name.find('.', dot + 1) == std::string_view::npos);
	}

	std::string_view base = dot == std::string_view::npos ? name : name.substr(0, dot);
	std::string_view extension = dot == std::string_view::npos ? std::string_view() : name.substr(dot + 1);
	Fat12Require<"a short name base has 1 to 8 characters">(base.size() >= 1 && base.size() <= 8);
	Fat12Require<"a short name extension has at most 3 characters">(extension.size() <= 3);

	for (std::size_t index = 0; index < base.size(); index++) {
		result[index] = Fat12CheckedChar(base[index]);
	}

	for (std::size_t index = 0; index < extension.size(); index++) {
		result[8 + index] = Fat12CheckedChar(extension[index]);
	}

	return result;
}


// The 11 bytes of the volume label. The label may hold spaces, and it is upper-cased like a name.
constexpr std::array<char, 11> Fat12LabelName(std::string_view label) {
	Fat12Require<"the volume label has 1 to 11 characters">(label.size() >= 1 && label.size() <= 11);

	std::array<char, 11> result{};
	for (char& value : result) {
		value = ' ';
	}

	for (std::size_t index = 0; index < label.size(); index++) {
		char upper = Fat12Upper(label[index]);
		Fat12Require<"the volume label holds only short-name characters and spaces">(upper == ' ' || Fat12IsShortNameChar(upper));
		result[index] = upper;
	}

	return result;
}


constexpr void Fat12Put8(Fat12Volume& volume, std::size_t offset, uint8 value) {
	volume.bytes[offset] = value;
}


constexpr void Fat12Put16(Fat12Volume& volume, std::size_t offset, uint32 value) {
	volume.bytes[offset] = static_cast<uint8>(value & 0xFF);
	volume.bytes[offset + 1] = static_cast<uint8>((value >> 8) & 0xFF);
}


constexpr void Fat12Put32(Fat12Volume& volume, std::size_t offset, uint32 value) {
	Fat12Put16(volume, offset, value & 0xFFFF);
	Fat12Put16(volume, offset + 2, value >> 16);
}


// Writes a name field of 11 bytes (the label in the boot sector, or a directory entry's name).
constexpr void Fat12PutName(Fat12Volume& volume, std::size_t offset, const std::array<char, 11>& name) {
	for (std::size_t index = 0; index < name.size(); index++) {
		volume.bytes[offset + index] = static_cast<uint8>(name[index]);
	}
}


// Writes an 8.3 directory entry (32 bytes). Times are zero and dates are Fat12DosDate. The cluster is the first one of the file, and
// zero for an empty file or the label.
constexpr void Fat12PutEntry(Fat12Volume& volume, std::size_t offset, const std::array<char, 11>& name, uint8 attribute, uint32 cluster, uint32 size) {
	Fat12PutName(volume, offset, name);
	Fat12Put8(volume, offset + 11, attribute);
	Fat12Put16(volume, offset + 16, Fat12DosDate);
	Fat12Put16(volume, offset + 18, Fat12DosDate);
	Fat12Put16(volume, offset + 24, Fat12DosDate);
	Fat12Put16(volume, offset + 26, cluster);
	Fat12Put32(volume, offset + 28, size);
}


// Stores one 12-bit FAT entry at index in the FAT that starts at byte fatOffset. Two entries share three bytes: an even entry takes
// the low byte and the low nibble of the next one, and an odd entry takes the high nibble of the byte before it and the next byte.
constexpr void Fat12StoreEntry(Fat12Volume& volume, std::size_t fatOffset, std::size_t index, uint16 value) {
	std::size_t offset = fatOffset + index + index / 2;
	if (index % 2 == 0) {
		volume.bytes[offset] = static_cast<uint8>(value & 0xFF);
		volume.bytes[offset + 1] = static_cast<uint8>((volume.bytes[offset + 1] & 0xF0) | ((value >> 8) & 0x0F));
	} else {
		volume.bytes[offset] = static_cast<uint8>((volume.bytes[offset] & 0x0F) | ((value & 0x0F) << 4));
		volume.bytes[offset + 1] = static_cast<uint8>((value >> 4) & 0xFF);
	}
}


constexpr uint8 Fat12ByteOf(const Fat12File& file, std::size_t index) {
	return file.isBytes ? file.bytes[index] : static_cast<uint8>(file.text[index]);
}


// Copies the first Size bytes of a built volume into an array of exact size.
template<std::size_t Size>
constexpr std::array<uint8, Size> Fat12Exact(const std::array<uint8, Fat12MaxBytes>& bytes) {
	std::array<uint8, Size> exact{};
	for (std::size_t index = 0; index < Size; index++) {
		exact[index] = bytes[index];
	}

	return exact;
}


class Fat12ImageBuilder {
private:
	uint32 sectorCount = 0;
	uint8 sectorsPerCluster = 1;
	uint8 fatCount = 2;
	uint16 rootEntryCount = 112;
	std::array<char, 11> label = Fat12LabelName("NO NAME");
	uint32 volumeId = 0x12345678;
	std::array<Fat12File, Fat12MaxFiles> files{};
	std::size_t fileCount = 0;


public:
	// The size of the volume in 512-byte sectors. Required.
	constexpr Fat12ImageBuilder& Sectors(uint32 count) {
		sectorCount = count;
		return *this;
	}


	// Sectors per cluster: 1, 2, 4, ..., 128. The default is 1, which suits a small volume.
	constexpr Fat12ImageBuilder& SectorsPerCluster(uint8 count) {
		Fat12Require<"sectors per cluster is a power of two from 1 to 128">(count >= 1 && count <= 128 && (count & (count - 1)) == 0);
		sectorsPerCluster = count;
		return *this;
	}


	// Copies of the FAT: 1 or 2. The default is 2, as Windows writes it.
	constexpr Fat12ImageBuilder& Fats(uint8 count) {
		Fat12Require<"a FAT has 1 or 2 copies">(count == 1 || count == 2);
		fatCount = count;
		return *this;
	}


	// Entries in the root directory, a multiple of 16. The label takes one of them. The default is 112.
	constexpr Fat12ImageBuilder& RootEntries(uint16 count) {
		Fat12Require<"root entries are a multiple of 16, at least 16">(count >= 16 && count % 16 == 0);
		rootEntryCount = count;
		return *this;
	}


	// The volume label, 1 to 11 characters. The default is "NO NAME".
	constexpr Fat12ImageBuilder& VolumeLabel(std::string_view text) {
		label = Fat12LabelName(text);
		return *this;
	}


	// The volume serial number in the boot sector.
	constexpr Fat12ImageBuilder& VolumeId(uint32 id) {
		volumeId = id;
		return *this;
	}


	// A file with text contents. The name is 8.3 and is upper-cased.
	constexpr Fat12ImageBuilder& File(std::string_view name, std::string_view text) {
		AddFile(name, text, std::span<const uint8>(), false, text.size());
		return *this;
	}


	// A file with byte contents. The span must refer to static storage, such as a namespace-scope constexpr array.
	constexpr Fat12ImageBuilder& File(std::string_view name, std::span<const uint8> bytes) {
		AddFile(name, std::string_view(), bytes, true, bytes.size());
		return *this;
	}


	// Lays out the volume and writes every byte of it: boot sector, FAT copies, root directory, and file data.
	constexpr Fat12Volume Build() const {
		Fat12Require<"a FAT12 image needs a size: call Sectors()">(sectorCount != 0);
		Fat12Require<"the image is larger than the builder capacity of 512 sectors">(sectorCount <= Fat12MaxSectors);

		std::size_t rootSectors = rootEntryCount / 16;
		std::size_t fatSectors = 1;
		std::size_t clusters = 0;

		// The FAT size depends on the cluster count, and the cluster count on the FAT size. Growing the FAT shrinks the data area,
		// so this settles in a few steps. It stops at the first FAT size that holds the clusters it leaves.
		for (std::size_t attempt = 0; attempt < 8; attempt++) {
			std::size_t overhead = 1 + fatCount * fatSectors + rootSectors;
			Fat12Require<"the volume has no room for data">(overhead < sectorCount);
			clusters = (sectorCount - overhead) / sectorsPerCluster;
			std::size_t fatBytes = ((clusters + 2) * 3 + 1) / 2;
			std::size_t needed = (fatBytes + Fat12SectorBytes - 1) / Fat12SectorBytes;
			if (needed <= fatSectors) {
				break;
			}

			fatSectors = needed;
		}

		Fat12Require<"the FAT does not hold the clusters of the volume">(((clusters + 2) * 3 + 1) / 2 <= fatSectors * Fat12SectorBytes);
		// Guards for a larger Fat12MaxSectors. At the cap of 512 sectors there are fewer than 512 clusters, so these two do not fire
		// until the cap is raised.
		Fat12Require<"a FAT12 volume has at most 4084 clusters">(clusters <= Fat12MaxClusters);
		Fat12Require<"the volume has no clusters for files">(clusters >= 1);
		// The boot sector's 16-bit sector count. It is a guard for the same reason as the check above.
		Fat12Require<"a FAT12 image holds at most 65535 sectors">(sectorCount <= 0xFFFF);

		std::size_t fatStart = 1;
		std::size_t rootStart = fatStart + fatCount * fatSectors;
		std::size_t dataStart = rootStart + rootSectors;
		std::size_t clusterBytes = static_cast<std::size_t>(sectorsPerCluster) * Fat12SectorBytes;

		Fat12Volume volume;
		volume.size = static_cast<std::size_t>(sectorCount) * Fat12SectorBytes;
		std::array<uint16, Fat12MaxClusters + 2> table{};
		table[0] = static_cast<uint16>(0xF00 | Fat12MediaFixed);
		table[1] = Fat12EndOfChain;

		WriteBootSector(volume, fatSectors);

		std::size_t rootOffset = rootStart * Fat12SectorBytes;
		Fat12PutEntry(volume, rootOffset, label, Fat12AttrLabel, 0, 0);
		std::size_t rootIndex = 1;
		std::size_t nextCluster = 2;

		for (std::size_t file = 0; file < fileCount; file++) {
			const Fat12File& entry = files[file];
			std::size_t count = (entry.size + clusterBytes - 1) / clusterBytes;
			Fat12Require<"the files do not fit in the volume">(nextCluster + count <= clusters + 2);
			Fat12Require<"the root directory is full">(rootIndex < rootEntryCount);

			std::size_t first = count == 0 ? 0 : nextCluster;

			for (std::size_t link = 0; link < count; link++) {
				bool last = link + 1 == count;
				table[nextCluster + link] = last ? Fat12EndOfChain : static_cast<uint16>(nextCluster + link + 1);
			}

			Fat12PutEntry(volume, rootOffset + rootIndex * 32, entry.shortName, Fat12AttrArchive, static_cast<uint32>(first), entry.size);

			std::size_t dataOffset = dataStart * Fat12SectorBytes + (first >= 2 ? (first - 2) * clusterBytes : 0);
			for (std::size_t index = 0; index < entry.size; index++) {
				volume.bytes[dataOffset + index] = Fat12ByteOf(entry, index);
			}

			nextCluster += count;
			rootIndex++;
		}

		for (std::size_t copy = 0; copy < fatCount; copy++) {
			std::size_t fatOffset = (fatStart + copy * fatSectors) * Fat12SectorBytes;
			for (std::size_t index = 0; index < clusters + 2; index++) {
				Fat12StoreEntry(volume, fatOffset, index, table[index]);
			}
		}

		return volume;
	}


private:
	constexpr void AddFile(std::string_view name, std::string_view text, std::span<const uint8> bytes, bool isBytes, std::size_t size) {
		Fat12Require<"the builder holds at most 8 files">(fileCount < Fat12MaxFiles);
		std::array<char, 11> shortName = Fat12FileName(name);

		for (std::size_t index = 0; index < fileCount; index++) {
			Fat12Require<"two files have the same short name">(files[index].shortName != shortName);
		}

		Fat12File entry;
		entry.shortName = shortName;
		entry.text = text;
		entry.bytes = bytes;
		entry.isBytes = isBytes;
		entry.size = size;
		files[fileCount] = entry;
		fileCount++;
	}


	// Boot sector: the jump, the OEM name, the BPB for FAT12 with 512-byte sectors, the extended BPB with the label and the
	// file-system type, and the signature 0x55 0xAA. The boot code is left zero.
	constexpr void WriteBootSector(Fat12Volume& volume, std::size_t fatSectors) const {
		Fat12Put8(volume, 0, 0xEB);
		Fat12Put8(volume, 1, 0x3C);
		Fat12Put8(volume, 2, 0x90);
		const char oem[] = "MSWIN4.1";
		for (std::size_t index = 0; index < 8; index++) {
			volume.bytes[3 + index] = static_cast<uint8>(oem[index]);
		}

		Fat12Put16(volume, 11, Fat12SectorBytes);
		Fat12Put8(volume, 13, sectorsPerCluster);
		Fat12Put16(volume, 14, 1);
		Fat12Put8(volume, 16, fatCount);
		Fat12Put16(volume, 17, rootEntryCount);
		Fat12Put16(volume, 19, sectorCount);
		Fat12Put8(volume, 21, Fat12MediaFixed);
		Fat12Put16(volume, 22, static_cast<uint32>(fatSectors));
		Fat12Put16(volume, 24, 32);
		Fat12Put16(volume, 26, 64);
		Fat12Put32(volume, 28, 0);
		Fat12Put32(volume, 32, 0);
		Fat12Put8(volume, 36, 0x80);
		Fat12Put8(volume, 37, 0);
		Fat12Put8(volume, 38, 0x29);
		Fat12Put32(volume, 39, volumeId);
		Fat12PutName(volume, 43, label);
		const char fileSystem[] = "FAT12   ";
		for (std::size_t index = 0; index < 8; index++) {
			volume.bytes[54 + index] = static_cast<uint8>(fileSystem[index]);
		}

		Fat12Put8(volume, 510, 0x55);
		Fat12Put8(volume, 511, 0xAA);
	}
};


// The bytes of one built volume, as a namespace-scope variable so the class template below can take its size.
template<Fat12ImageBuilder (*Make)()>
inline constexpr Fat12Volume Fat12BuiltVolume = Make().Build();


// The volume a builder chain describes, as an array of exact size. Size and Bytes are static constexpr, so the bytes stay in
// flash and the spans of Bytes stay valid for the life of the program.
template<Fat12ImageBuilder (*Make)()>
class Fat12Image {
public:
	static constexpr std::size_t Size = Fat12BuiltVolume<Make>.size;
	static constexpr std::array<uint8, Size> Bytes = Fat12Exact<Size>(Fat12BuiltVolume<Make>.bytes);
};
