# Mass Storage Class

A mass storage function: one logical unit of a block device (a RAM disk, flash, an SD card) on a USB host through the Bulk-Only Transport and the SCSI transparent command set. Windows, Linux and macOS bind their in-box drivers to the interface, so the device needs no driver of its own, and the host mounts the volume like a removable disk. The stack is tested in the host simulator only; no hardware measurement is recorded for mass storage yet (see [Host Support](../../README.md#host-support)).

Headers:

- `#include <Drivers/Interface/USB/Class/MSC/USBMscBuilder.h>`: the interface, for the configuration builder
- `#include <Drivers/Interface/USB/Class/MSC/MscFunction.h>`: the function, the Bulk-Only Transport and the SCSI commands
- `#include <Drivers/Interface/USB/Class/MSC/MscProtocol.h>`: the wire values (class, CBW and CSW layout, opcodes, sense codes)

The medium is an `IBlockDevice` (see [Block Device](../../../../../Memory/BlockDevice/README.md)). A volume for a RAM disk is built with the [FAT12 image builder](../../../../../../Utilities/FileSystem/README.md).

## Descriptors

`USBMscBuilder` writes one interface of class 0x08, subclass 0x06 (SCSI transparent) and protocol 0x50 (Bulk-Only), with a bulk IN and a bulk OUT endpoint of 64-byte packets and interval 0. The IN endpoint comes first in the layout.

```cpp
using AppUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		// ... identities ...
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMscBuilder().Name("Mass storage").BulkIn(0x81).BulkOut(0x02)));
}>;
```

| Method | Description |
|--------|-------------|
| `BulkIn(address)` | The IN address, bit 7 set and endpoint number 1 to 15. Without it the configuration builder assigns one |
| `BulkOut(address)` | The OUT address, bit 7 clear and endpoint number 1 to 15 |
| `Name(text)` | The interface string. The string is numbered like any other, so the same text as the product reuses its index |

Without a name the interface string is 0. The function does not need WinUSB, so no MS OS 2.0 descriptor is written for it.

## Function

`MscFunction<BufferSize>` takes the layout of the function, the block device and the identity the host reads in INQUIRY:

```cpp
RamBlockDevice<512, 128> disk(Image::Bytes);
MscFunction<4096> msc(AppUsb::Function(0), disk, MscIdentity{ "VHAL", "RAM Disk", "1.00", true });
```

`BufferSize` is the data buffer in bytes. It is one data stage of one transfer, so it is the largest chunk the medium is read or written in. It must be a multiple of the packet size (64) and of the block size of the device; a device whose block size does not divide it fails `OnConfigured()` with `invalidParameter`.

`MscIdentity` holds the vendor (cut or padded to 8 characters), the product (16) and the revision (4), and the removable bit. The strings must refer to static storage.

| Method | Description |
|--------|-------------|
| `SetEjectDelegate(delegate)` | Called from the interrupt when the host ejects the medium. Set before the device starts: `busy` once configured |
| `Process(context)` | From the loop. Reads the medium's presence and write protection, then does one chunk of the medium work a command needs |

The function keeps no pointer to the device. The application's loop calls `device.Process()`, which calls the function's `Process()`.

## Bulk-Only Transport

The function follows BOT 1.0. A command runs in these steps:

1. The CBW (31 bytes, signature `USBC`) arrives on the OUT endpoint. A CBW that is not 31 bytes long, or has another signature, is not valid. Both pipes are stalled and the function waits for a reset; a host clear of the halt alone does not reopen the pipes. A CBW that is valid but not meaningful (another LUN, a reserved flag bit, a command length outside 1 to 16) fails as a command with no data of the device's own.
2. The device decides the command from the CBW and works out what it intends to move (its direction and its length). The host's length and direction (`dCBWDataTransferLength`, the Direction bit) are compared with that, and the case of the thirteen in BOT section 6.7 is chosen before any data moves.
3. The data stage moves, in chunks of at most `BufferSize` bytes.
4. The CSW (13 bytes, signature `USBS`, the tag of the CBW, the residue and the status) goes on the IN endpoint.

The residue is the host's length minus the bytes of the data stage that moved. The status is `0x00` (passed), `0x01` (failed) or `0x02` (phase error).

| Case | Host and device | What the function does | Status | Residue |
|------|-----------------|------------------------|--------|---------|
| 1 | Hn = Dn | Nothing to move | Command's | 0 |
| 2 | Hn < Di | No data, the answer is not sent | Phase error | 0 |
| 3 | Hn < Do | No data, the medium is not written | Phase error | 0 |
| 4 | Hi > Dn | Stalls IN. The CSW follows the host's clear | Command's | Hi |
| 5 | Hi > Di | Sends Di bytes, stalls IN. The CSW follows the clear | Command's | Hi - Di |
| 6 | Hi = Di | Sends Di bytes, then the CSW | Command's | 0 |
| 7 | Hi < Di | Sends Hi bytes, then the CSW | Phase error | 0 |
| 8 | Hi <> Do | Stalls IN. The CSW follows the clear. The medium is not written | Phase error | Hi |
| 9 | Ho > Dn | Stalls OUT, then the CSW | Command's | Ho |
| 10 | Ho <> Di | Stalls OUT, then the CSW | Phase error | Ho |
| 11 | Ho > Do | Takes Do bytes, stalls OUT after the last chunk, writes it, then the CSW | Command's | Ho - Do |
| 12 | Ho = Do | Takes Do bytes, writes them, then the CSW | Command's | 0 |
| 13 | Ho < Do | Stalls OUT, then the CSW. The medium is not written | Phase error | Ho |

Where the table says the CSW follows a clear, the host clears the halt with `CLEAR_FEATURE(ENDPOINT_HALT)`, and the CSW is sent when the clear arrives. A phase error means the host performs a Bulk-Only Mass Storage Reset.

Reset recovery: a Bulk-Only Mass Storage Reset (`bRequest` 0xFF, `wValue` 0, `wLength` 0, on the interface) abandons the command in progress, stalls any data pipe the function had armed, and returns to the idle stage. The host then clears both halts. The function does not clear the halts itself.

GET_MAX_LUN (`bRequest` 0xFE, one byte) answers 0. The class requests reach the function once the configuration selects it.

## SCSI Commands

| Command | Answer |
|---------|--------|
| TEST UNIT READY | Passes, or NOT READY, MEDIUM NOT PRESENT when no medium is inserted |
| REQUEST SENSE | Fixed format (0x70), the key, ASC and ASCQ of the last error, 18 bytes |
| INQUIRY | 36 bytes: removable bit from the identity, SPC-2, vendor, product and revision. EVPD and CMDT are an invalid field |
| READ CAPACITY(10) | Last block and block length, big endian |
| READ FORMAT CAPACITIES | One descriptor: the block count, code 2 (formatted media) or 3 (no media), and the block length |
| MODE SENSE(6), (10) | The header alone, with the write-protect bit set on a write-protected medium. Page 0x3F only; any other page is an invalid field |
| PREVENT ALLOW MEDIUM REMOVAL | Stores the prevent flag |
| START STOP UNIT | LOEJ with START clear ejects: refused with MEDIUM REMOVAL PREVENTED while prevented, otherwise the eject delegate runs and the medium is not present. START and LOAD pass |
| READ(10), WRITE(10) | Block range checked (LBA out of range). A write to a write-protected medium fails with DATA PROTECT, WRITE PROTECTED before any data moves |
| VERIFY(10) | Reads the blocks. BYTCHK is an invalid field |
| SYNCHRONIZE CACHE(10) | Calls `Sync()` on the medium |
| anything else | CHECK CONDITION, ILLEGAL REQUEST, INVALID COMMAND OPERATION CODE |

The sense data of an error is kept until the next command. REQUEST SENSE reads it without clearing it; any other command clears it before it runs.

## Medium Access and Context

Media access runs only in `Process()`, from the application's loop. The interrupt decides the command from the CBW and the presence and write protection `Process()` last read. The endpoints NAK while `Process()` works on the medium:

- A read is one chunk: `Process()` reads the blocks into the buffer, then arms the IN endpoint. Until then the host gets NAK.
- A write is one chunk: the OUT endpoint takes the chunk into the buffer and then waits. `Process()` writes the chunk, then arms the next one. Until then the host gets NAK.

The presence is read by the constructor and again by each `Process()`. An eject makes the function report the medium absent at once and calls the eject delegate from the interrupt. The delegate should make the block device's `IsPresent()` return `notExist`, so the next `Process()` agrees with the function.

| Code | Context | Rule |
|------|---------|------|
| Endpoint callbacks, `OnControlRequest`, `OnConfigured`, `OnDeconfigured`, `OnBusReset` | Interrupt | Short. No medium access, no blocking call. Only the state and the endpoint calls |
| Eject delegate | Interrupt | Short. It runs from START STOP UNIT |
| `Process()` | Thread | Takes a section to plan the medium work and another to commit its result. The medium access itself is outside every section. A reset that happens during the access drops the result |
| `SetEjectDelegate()` | Thread | Before the device starts |

## Limits

- One logical unit (LUN 0). Get Max LUN answers 0.
- The medium's block size and count are read once, in the constructor. A medium of another size is a different function.
- No VPD pages, no byte-compare VERIFY, no mode pages but the header. The PMI bit of READ CAPACITY is ignored.
- Each command moves at most `BufferSize` bytes per `Process()` call, so a large transfer takes as many calls as it has chunks.
- The function does not cache data. Each chunk is read from or written to the medium.

## Tests

The host tests are in `.tool/sim` and run with `run.sh`. They use a BOT host model (`MscHost.h`) that runs the device's `Process()` on each NAK, the way a main loop would, and clears halts through the device core's `CLEAR_FEATURE`:

- `TestMscDescriptors.cpp`: the device and configuration bytes, the function layout and the assigned endpoint addresses, against bytes written out by hand.
- `TestMscEnumeration.cpp`: the Windows, Linux and macOS enumeration orders, GET_MAX_LUN, a TEST UNIT READY loop for a medium inserted late, and the mounting reads (INQUIRY, READ CAPACITY, READ FORMAT CAPACITIES, MODE SENSE, READ(10) of block 0).
- `TestMscBot.cpp`: each of the thirteen cases of section 6.7 with the data, the CSW and the stall counts it must produce, the invalid CBWs, reset recovery, an armed read reset, an unknown opcode, and CBWs that are not meaningful.
- `TestMscScsi.cpp`: each command's answer, the sense data kept per error, the eject and the prevent, VERIFY and SYNCHRONIZE CACHE, and a read past the end.
- `TestMscMedia.cpp`: round trips across the buffer boundaries (one-block and eight-block chunks), partial reads in both directions, the NAK while `Process()` reads and writes, write protection, read and write errors in the middle of a transfer, and a reset with a write armed.
- `BuilderErrors/msc_*.cpp`: the builder's endpoint rules, each with the message it must give.
