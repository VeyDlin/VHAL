# FAT12 Image Builder

A FAT12 volume built at compile time, so a small RAM disk mounts on Windows, Linux and macOS without formatting. The boot sector has a BPB for 512-byte sectors, one or two FAT copies, a root directory with a volume label entry, and the files the application gave. The bytes are computed by the compiler and stored in flash with the application, the same way the USB descriptors are.

Header: `#include <Utilities/FileSystem/Fat12ImageBuilder.h>`

## Usage

The chain is a captureless lambda, as for `USBDescriptors`. `Fat12Image` gives the bytes in an array of exact size:

```cpp
namespace Data {
inline constexpr uint8 Blob[] = { 1, 2, 3, 0x80, 0xFF };
}

using Image = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(128)
		.VolumeLabel("VHAL DISK")
		.File("readme.txt", "Hello from the RAM disk.\r\n")
		.File("blob.bin", std::span<const uint8>(Data::Blob));
}>;

RamBlockDevice<512, 128> disk(Image::Bytes);
```

The file contents must be static: a string literal, or a namespace-scope constexpr array. Inside the lambda a static local is not allowed, because the chain is evaluated in a constant expression.

## Builder

| Method | Default | Description |
|--------|---------|-------------|
| `Sectors(count)` | none, required | The volume size in 512-byte sectors. At most 512, so the volume is at most 256 KiB |
| `SectorsPerCluster(count)` | 1 | 1, 2, 4, ... 128 |
| `Fats(count)` | 2 | 1 or 2 copies of the FAT |
| `RootEntries(count)` | 112 | A multiple of 16, at least 16. The label takes one entry |
| `VolumeLabel(text)` | `NO NAME` | 1 to 11 characters, upper-cased. Spaces are allowed |
| `VolumeId(id)` | `0x12345678` | The serial number in the boot sector |
| `File(name, text)` | | A file with text contents |
| `File(name, bytes)` | | A file with byte contents, from a `std::span<const uint8>` |

Names are 8.3: a base of 1 to 8 characters and an extension of at most 3, with at most one dot. Lower-case letters are upper-cased. The characters allowed are the upper-case letters, the digits and `!#$%&'()-@^_`{}~`.

## Layout

The layout is computed from the size, and it is the same on every build:

- Sector 0 is the boot sector: the jump, the OEM name `MSWIN4.1`, the BPB (512 bytes per sector, the cluster size, one reserved sector, the FAT count, the root entries, the 16-bit sector count, media `0xF8`, the FAT size, a geometry of 32 sectors and 64 heads, drive `0x80`), the extended BPB with the serial number, the label and `FAT12`, the boot signature, and the `0x55 0xAA` marker. The boot code is zero.
- The FAT copies follow the reserved sector. Entries 0 and 1 are `0xFF8` and `0xFFF`. A file's clusters are consecutive from 2, and its last cluster is marked `0xFFF`.
- The root directory holds the label entry first, then one 32-byte entry per file in the order given. Each entry has the 8.3 name, the attribute (`0x08` for the label, `0x20` for a file), the first cluster and the size. Every date is 1980-01-01, and every time is zero.
- The data area follows the root directory. An empty file has cluster 0 and no chain.

The FAT size grows with the cluster count, and the cluster count shrinks as the FAT grows, so the builder settles on the smallest FAT that holds the clusters it leaves. A volume of 128 sectors has one FAT sector per copy; 512 sectors have two.

## Rules

Each rule is checked at compile time, and a violation is an error with its message (see `BuilderErrors/fat12_*.cpp`):

| Rule | Message |
|------|---------|
| `Sectors()` is required | a FAT12 image needs a size: call Sectors() |
| At most 512 sectors | the image is larger than the builder capacity of 512 sectors |
| The FAT fits a 16-bit sector count | a FAT12 image holds at most 65535 sectors |
| Fewer than 4085 clusters, which makes the volume FAT12 | a FAT12 volume has at most 4084 clusters |
| Sectors per cluster is a power of two from 1 to 128 | sectors per cluster is a power of two from 1 to 128 |
| One or two FAT copies | a FAT has 1 or 2 copies |
| Root entries a multiple of 16, at least 16 | root entries are a multiple of 16, at least 16 |
| Label of 1 to 11 characters, short-name characters and spaces | the volume label has 1 to 11 characters |
| A base of 1 to 8 characters, an extension of at most 3 | a short name base has 1 to 8 characters |
| One dot at most | a file name has at most one dot |
| Only the short-name characters | a short name character is not allowed |
| At most 8 files | the builder holds at most 8 files |
| No two files with the same short name | two files have the same short name |
| The files fit the data area | the files do not fit in the volume |
| The files fit the root directory | the root directory is full |

## Result

`Fat12Image<+[] { ... }>` has two static members:

- `Size`: the volume's size in bytes (sectors times 512).
- `Bytes`: the volume as an array of exact size, in flash.

`Fat12ImageBuilder::Build()` returns the same bytes as a `Fat12Volume` (an array of `Fat12MaxBytes` and the size in use), for a host-side tool that wants the image in a file.

## Limits

- FAT12 only, with 512-byte sectors. The 16-bit sector count is the limit, so the image is at most 65535 sectors, and the builder caps it at 512.
- Short names only. No long file names, no subdirectories, no timestamps other than 1980-01-01.
- The boot code is zero: the volume is data, not bootable.
- Builder capacity: 8 files, 512 sectors. The chain itself is evaluated at compile time, so a large image takes a longer build.

## Tests

`.tool/sim/TestFat12ImageBuilder.cpp` builds the volumes at compile time and parses them back with a reader written in the test, from the FAT specification rather than from the builder. It checks the BPB fields, the signature, both FAT copies, the 12-bit entries, the label and the file entries, and the cluster chains, including one that crosses a FAT sector boundary. The rules are checked by the builder-error snippets in `BuilderErrors/fat12_*.cpp`.
