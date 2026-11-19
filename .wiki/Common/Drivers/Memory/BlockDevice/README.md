# Block Device

A medium of equal blocks: a RAM disk, a flash disk, an SD card. A consumer reads and writes whole blocks through the interface, and the mass storage function (see [Mass Storage Class](../../Interface/USB/Class/MSC/README.md)) is the first consumer. The interface is the same for every medium, so the function does not change with the hardware.

Headers:

- `#include <Drivers/Memory/BlockDevice/IBlockDevice.h>`: the interface
- `#include <Drivers/Memory/BlockDevice/RamBlockDevice.h>`: a medium in RAM

## Interface

`IBlockDevice` has the block size and the block count, the three operations that move or make data durable, and the two state queries:

| Method | Returns | Description |
|--------|---------|-------------|
| `GetBlockSize()` | `uint32` | Bytes per block. Constant for the life of the object |
| `GetBlockCount()` | `uint32` | Number of blocks, numbered from 0. Constant for the life of the object |
| `Read(firstBlock, data)` | `ResultStatus` | Reads `data.size() / GetBlockSize()` blocks into `data` |
| `Write(firstBlock, data)` | `ResultStatus` | Writes whole blocks from `data`. `readOnly` on a write-protected medium |
| `Sync()` | `ResultStatus` | Makes the written blocks durable. A medium with no write cache returns `ok` |
| `IsReadOnly()` | `ResultStatus` | `ok` when the medium accepts writes, `readOnly` when it is write protected |
| `IsPresent()` | `ResultStatus` | `ok` when a medium is inserted, `notExist` when it is not |

Every operation that can fail returns `ResultStatus`, as the memory drivers do. The two queries return a status rather than a `bool`, so a caller compares them with the value it means: `IsPresent() == ResultStatus::ok`, `IsReadOnly() == ResultStatus::readOnly`.

Rules:

- `Read` and `Write` take whole blocks. A `data.size()` that is not a multiple of the block size is `invalidParameter`. A range past the last block is `outOfRange`. Both checks are in `CheckRange()`, which the implementations share.
- A call on a medium that is not present returns `notExist`.
- Every call is thread context and may block, because a flash or card access takes time. An interrupt must not call them. The mass storage function calls them only from its `Process()`, and it reads `IsPresent()` and `IsReadOnly()` there and keeps the values for its interrupt.

## RAM Disk

`RamBlockDevice<BlockSize, BlockCount>` keeps `BlockCount` blocks of `BlockSize` bytes in a member array:

```cpp
RamBlockDevice<512, 128> disk(Image::Bytes);
```

- The image constructor copies a volume into the start of the device. A shorter image leaves the rest zero; a longer one is cut to the capacity. The FAT12 builder's `Image::Bytes` is the usual argument (see [FAT12 Image Builder](../../../../Utilities/FileSystem/README.md)).
- `SetReadOnly(bool)` write-protects the medium: `Write()` returns `readOnly`, and `IsReadOnly()` returns `readOnly`.
- `SetPresent(bool)` models an ejected medium: `IsPresent()`, `Read()`, `Write()` and `Sync()` return `notExist` while it is false.
- `Storage()` gives the whole storage as a span, for a test or an application that reads the medium directly.

The storage is in the object, so a large device takes that much RAM. Use the builder's volume, or a smaller block count, for the RAM disks of a small target.

## Implementing a Medium

A flash or card driver implements `IBlockDevice` and keeps the rules above. The mass storage function reads `GetBlockSize()` and `GetBlockCount()` in its constructor, so those must be valid before the function is built. `IsPresent()` and `IsReadOnly()` are read again at each `Process()`.

## Tests

The RAM disk is tested through the mass storage tests in `.tool/sim` (`TestMscMedia.cpp`), which use a disk with a fault switch (`FaultyDisk`) that fails the access to a chosen block. The FAT12 image that the disk mounts is checked by `TestFat12ImageBuilder.cpp`.
