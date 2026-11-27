#include <VHAL.h>
#include "SimAssert.h"
#include <Utilities/FileSystem/Fat12ImageBuilder.h>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>


// The FAT12 image builder checked against a reader written here. The reader knows the format from the specification of the
// FAT file system (the BPB offsets, the 12-bit FAT entries and the 32-byte directory entries), not from the builder, so a
// mistake in the builder's layout arithmetic cannot hide in both. The volumes are built at compile time.


namespace {


// A file of 1300 bytes: three clusters on a 512-byte cluster, with the last one partly used.
constexpr std::array<uint8, 1300> MakePattern(uint32 seed) {
	std::array<uint8, 1300> bytes{};
	for (std::size_t index = 0; index < bytes.size(); index++) {
		bytes[index] = static_cast<uint8>((index * 7 + seed) & 0xFF);
	}

	return bytes;
}


// Two files of 100 KiB each. The second one's chain crosses a FAT sector boundary (entry 341 straddles the boundary between the first and second FAT sectors, in bytes 511 and 512).
constexpr std::array<uint8, 100 * 1024> MakeBlock(uint32 seed) {
	std::array<uint8, 100 * 1024> bytes{};
	for (std::size_t index = 0; index < bytes.size(); index++) {
		bytes[index] = static_cast<uint8>((index * 13 + seed) & 0xFF);
	}

	return bytes;
}


inline constexpr std::array<uint8, 1300> LongFile = MakePattern(3);
inline constexpr std::array<uint8, 5> BlobFile = { 1, 2, 3, 0x80, 0xFF };
inline constexpr std::array<uint8, 100 * 1024> FirstBlock = MakeBlock(5);
inline constexpr std::array<uint8, 100 * 1024> SecondBlock = MakeBlock(11);
constexpr std::string_view ReadmeText = "Hello from the RAM disk.\r\n";


using MainImage = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(128)
		.VolumeLabel("VHAL DISK")
		.File("readme.txt", ReadmeText)
		.File("long.bin", std::span<const uint8>(LongFile))
		.File("empty.dat", "")
		.File("blob.bin", std::span<const uint8>(BlobFile));
}>;


using SmallClusterImage = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(256)
		.SectorsPerCluster(2)
		.Fats(1)
		.RootEntries(32)
		.VolumeLabel("ONE FAT")
		.File("data.bin", std::span<const uint8>(LongFile));
}>;


using LargeFatImage = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(512)
		.VolumeLabel("BIG")
		.File("first.bin", std::span<const uint8>(FirstBlock))
		.File("second.bin", std::span<const uint8>(SecondBlock));
}>;


// The reader. It works on the bytes of a volume and returns what the format says is there.
class Reader {
private:
	const uint8* bytes;
	std::size_t size;


public:
	Reader(std::span<const uint8> image)
		: bytes(image.data()),
		  size(image.size()) {
	}


	uint16 U16(std::size_t offset) const {
		return static_cast<uint16>(Byte(offset) | (Byte(offset + 1) << 8));
	}


	uint32 U32(std::size_t offset) const {
		return static_cast<uint32>(U16(offset) | (static_cast<uint32>(U16(offset + 2)) << 16));
	}


	// A byte of the volume. An offset outside the volume is a failed check, and the byte reads as zero, so a broken chain fails by name.
	uint8 Byte(std::size_t offset) const {
		if (offset >= size) {
			SIM_EXPECT(offset < size);
			return 0;
		}

		return bytes[offset];
	}


	std::size_t Size() const {
		return size;
	}


	// One 12-bit FAT entry. Entry n sits at byte 3n/2: an even entry is the low 12 bits of the 16 bits there, an odd entry the high 12.
	uint16 FatEntry(std::size_t fatOffset, std::size_t index) const {
		std::size_t offset = fatOffset + (index * 3) / 2;
		if (index % 2 == 0) {
			return static_cast<uint16>(Byte(offset) | ((Byte(offset + 1) & 0x0F) << 8));
		}

		return static_cast<uint16>((Byte(offset) >> 4) | (Byte(offset + 1) << 4));
	}
};


struct Entry {
	std::string name;
	uint8 attribute = 0;
	uint16 cluster = 0;
	uint32 size = 0;
};


std::string Name(const Reader& reader, std::size_t offset) {
	std::string name;
	for (std::size_t index = 0; index < 11; index++) {
		name.push_back(static_cast<char>(reader.Byte(offset + index)));
	}

	return name;
}


Entry ReadEntry(const Reader& reader, std::size_t offset) {
	Entry entry;
	entry.name = Name(reader, offset);
	entry.attribute = reader.Byte(offset + 11);
	entry.cluster = reader.U16(offset + 26);
	entry.size = reader.U32(offset + 28);
	return entry;
}


// The byte offsets of a volume from its BPB, computed the way the format defines them.
struct Geometry {
	std::size_t fatOffset = 0;
	std::size_t fatBytes = 0;
	std::size_t rootOffset = 0;
	std::size_t dataOffset = 0;
	std::size_t clusterBytes = 0;
};


Geometry GeometryOf(const Reader& reader) {
	Geometry geometry;
	std::size_t bytesPerSector = reader.U16(11);
	std::size_t sectorsPerCluster = reader.Byte(13);
	std::size_t reserved = reader.U16(14);
	std::size_t fats = reader.Byte(16);
	std::size_t rootEntries = reader.U16(17);
	std::size_t fatSectors = reader.U16(22);
	std::size_t rootSectors = (rootEntries * 32 + bytesPerSector - 1) / bytesPerSector;
	geometry.fatOffset = reserved * bytesPerSector;
	geometry.fatBytes = fatSectors * bytesPerSector;
	geometry.rootOffset = (reserved + fats * fatSectors) * bytesPerSector;
	geometry.dataOffset = geometry.rootOffset + rootSectors * bytesPerSector;
	geometry.clusterBytes = sectorsPerCluster * bytesPerSector;
	return geometry;
}


// The bytes of a file, read by following its chain in the FAT. Stops at an end-of-chain mark.
std::vector<uint8> ReadChain(const Reader& reader, const Geometry& geometry, uint16 first, uint32 size) {
	std::vector<uint8> data;
	uint16 cluster = first;
	std::size_t guard = 0;

	while (data.size() < size) {
		// A cluster outside the data area is a broken chain: the read stops, and the comparison with the file fails.
		if (cluster < 2 || guard > 4096 || geometry.dataOffset + (cluster - 2) * geometry.clusterBytes + geometry.clusterBytes > reader.Size()) {
			break;
		}

		std::size_t offset = geometry.dataOffset + (cluster - 2) * geometry.clusterBytes;
		for (std::size_t index = 0; index < geometry.clusterBytes && data.size() < size; index++) {
			data.push_back(reader.Byte(offset + index));
		}

		cluster = reader.FatEntry(geometry.fatOffset, cluster);
		guard++;
	}

	return data;
}


bool SameFile(const std::vector<uint8>& actual, std::span<const uint8> expected) {
	if (actual.size() != expected.size()) {
		return false;
	}

	for (std::size_t index = 0; index < expected.size(); index++) {
		if (actual[index] != expected[index]) {
			return false;
		}
	}

	return true;
}


void TestBootSector() {
	Reader reader(MainImage::Bytes);

	SIM_EXPECT_EQ(MainImage::Size, 128 * 512);
	SIM_EXPECT_EQ(reader.Byte(0), 0xEB);
	SIM_EXPECT_EQ(reader.Byte(1), 0x3C);
	SIM_EXPECT_EQ(reader.Byte(2), 0x90);
	SIM_EXPECT_EQ(reader.U16(11), 512);
	SIM_EXPECT_EQ(reader.Byte(13), 1);
	SIM_EXPECT_EQ(reader.U16(14), 1);
	SIM_EXPECT_EQ(reader.Byte(16), 2);
	SIM_EXPECT_EQ(reader.U16(17), 112);
	SIM_EXPECT_EQ(reader.U16(19), 128);
	SIM_EXPECT_EQ(reader.Byte(21), 0xF8);
	SIM_EXPECT_EQ(reader.U16(22), 1);
	SIM_EXPECT_EQ(reader.U32(28), 0);
	SIM_EXPECT_EQ(reader.U32(32), 0);
	SIM_EXPECT_EQ(reader.Byte(36), 0x80);
	SIM_EXPECT_EQ(reader.Byte(38), 0x29);
	SIM_EXPECT_EQ(reader.U32(39), 0x12345678);
	SIM_EXPECT(Name(reader, 43) == "VHAL DISK  ");
	SIM_EXPECT(MainImage::Bytes[54] == 'F' && MainImage::Bytes[55] == 'A' && MainImage::Bytes[56] == 'T');
	SIM_EXPECT(MainImage::Bytes[57] == '1' && MainImage::Bytes[58] == '2');
	SIM_EXPECT_EQ(reader.Byte(510), 0x55);
	SIM_EXPECT_EQ(reader.Byte(511), 0xAA);
}


void TestFatAndDirectory() {
	Reader reader(MainImage::Bytes);
	Geometry geometry = GeometryOf(reader);

	// Two FAT copies of one sector each, identical.
	SIM_EXPECT_EQ(geometry.fatBytes, 512);
	for (std::size_t index = 0; index < 512; index++) {
		SIM_EXPECT_EQ(reader.Byte(geometry.fatOffset + index), reader.Byte(geometry.fatOffset + 512 + index));
	}

	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 0), 0xFF8);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 1), 0xFFF);

	// readme.txt: 26 bytes, one cluster, the chain ends at once. Cluster 2.
	// long.bin: 1300 bytes, three clusters 3, 4, 5.
	// empty.dat: no cluster. blob.bin: 5 bytes, cluster 6.
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 2), 0xFFF);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 3), 4);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 4), 5);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 5), 0xFFF);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 6), 0xFFF);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 7), 0);

	// The root directory: the label first, then one entry per file in the order they were given.
	std::size_t root = geometry.rootOffset;
	Entry label = ReadEntry(reader, root);
	SIM_EXPECT(label.name == "VHAL DISK  ");
	SIM_EXPECT_EQ(label.attribute, 0x08);

	Entry readme = ReadEntry(reader, root + 32);
	SIM_EXPECT(readme.name == "README  TXT");
	SIM_EXPECT_EQ(readme.attribute, 0x20);
	SIM_EXPECT_EQ(readme.cluster, 2);
	SIM_EXPECT_EQ(readme.size, ReadmeText.size());
	SIM_EXPECT(SameFile(ReadChain(reader, geometry, readme.cluster, readme.size), std::span<const uint8>(
		reinterpret_cast<const uint8*>(ReadmeText.data()), ReadmeText.size())));

	Entry longFile = ReadEntry(reader, root + 64);
	SIM_EXPECT(longFile.name == "LONG    BIN");
	SIM_EXPECT_EQ(longFile.cluster, 3);
	SIM_EXPECT_EQ(longFile.size, 1300);
	SIM_EXPECT(SameFile(ReadChain(reader, geometry, longFile.cluster, longFile.size), LongFile));

	Entry empty = ReadEntry(reader, root + 96);
	SIM_EXPECT(empty.name == "EMPTY   DAT");
	SIM_EXPECT_EQ(empty.cluster, 0);
	SIM_EXPECT_EQ(empty.size, 0);

	Entry blob = ReadEntry(reader, root + 128);
	SIM_EXPECT(blob.name == "BLOB    BIN");
	SIM_EXPECT_EQ(blob.cluster, 6);
	SIM_EXPECT(SameFile(ReadChain(reader, geometry, blob.cluster, blob.size), BlobFile));

	// The entry after the last file is free: its first byte is zero.
	SIM_EXPECT_EQ(reader.Byte(root + 160), 0);
}


void TestSmallClusterOneFat() {
	Reader reader(SmallClusterImage::Bytes);
	Geometry geometry = GeometryOf(reader);

	SIM_EXPECT_EQ(reader.Byte(13), 2);
	SIM_EXPECT_EQ(reader.Byte(16), 1);
	SIM_EXPECT_EQ(reader.U16(17), 32);
	SIM_EXPECT_EQ(reader.U16(22), 1);
	SIM_EXPECT_EQ(geometry.clusterBytes, 1024);

	// One FAT, so the root starts after one FAT sector and holds 32 entries in 2 sectors.
	SIM_EXPECT_EQ(geometry.rootOffset, 512 * 2);
	SIM_EXPECT_EQ(geometry.dataOffset, 512 * 4);

	Entry label = ReadEntry(reader, geometry.rootOffset);
	SIM_EXPECT(label.name == "ONE FAT    ");
	SIM_EXPECT_EQ(label.attribute, 0x08);

	// data.bin: 1300 bytes on 1024-byte clusters is two clusters, 2 and 3.
	Entry data = ReadEntry(reader, geometry.rootOffset + 32);
	SIM_EXPECT(data.name == "DATA    BIN");
	SIM_EXPECT_EQ(data.cluster, 2);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 2), 3);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 3), 0xFFF);
	SIM_EXPECT(SameFile(ReadChain(reader, geometry, data.cluster, data.size), LongFile));
}


void TestLargeFatAcrossSectors() {
	Reader reader(LargeFatImage::Bytes);
	Geometry geometry = GeometryOf(reader);

	// 512 sectors leave 500 clusters, so the FAT is two sectors per copy.
	SIM_EXPECT_EQ(reader.U16(22), 2);
	SIM_EXPECT_EQ(geometry.fatBytes, 1024);

	Entry first = ReadEntry(reader, geometry.rootOffset + 32);
	Entry second = ReadEntry(reader, geometry.rootOffset + 64);
	SIM_EXPECT(first.name == "FIRST   BIN");
	SIM_EXPECT(second.name == "SECOND  BIN");

	// The first file occupies clusters 2 to 201 and the second 202 to 401. Entry 341 is odd and its 12 bits sit in bytes 511 and 512,
	// so it straddles the boundary between the first and second FAT sectors. The chain through it checks the packing across that byte.
	SIM_EXPECT_EQ(first.cluster, 2);
	SIM_EXPECT_EQ(second.cluster, 202);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 339), 340);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 340), 341);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 341), 342);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 342), 343);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 401), 0xFFF);
	SIM_EXPECT_EQ(reader.FatEntry(geometry.fatOffset, 402), 0);

	SIM_EXPECT(SameFile(ReadChain(reader, geometry, first.cluster, first.size), FirstBlock));
	SIM_EXPECT(SameFile(ReadChain(reader, geometry, second.cluster, second.size), SecondBlock));
}


void TestCopiesMatch() {
	// Two FAT copies of the large volume are identical, byte for byte.
	Reader reader(LargeFatImage::Bytes);
	Geometry geometry = GeometryOf(reader);
	for (std::size_t index = 0; index < geometry.fatBytes; index++) {
		SIM_EXPECT_EQ(reader.Byte(geometry.fatOffset + index), reader.Byte(geometry.fatOffset + geometry.fatBytes + index));
	}
}


}


int main() {
	TestBootSector();
	TestFatAndDirectory();
	TestSmallClusterOneFat();
	TestLargeFatAcrossSectors();
	TestCopiesMatch();
	return SimTest::Finish("TestFat12ImageBuilder");
}
