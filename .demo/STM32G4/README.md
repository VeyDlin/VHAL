# STM32G4 demo (STM32G431)

A demo project for the STM32G4 family on VHAL, next to `.demo/STM32G0`. It holds USB device demos for the firmware of the
USB device stack (`Common/Drivers/Interface/USB`), and the regex board check (`Common/Utilities/Regex`). One demo is built at a
time, chosen with the CMake cache variable `VHAL_DEMO`:

- `USBBench` (default): the USB benchmark. It enumerates as pid.codes `1209:0001` with one WinUSB vendor interface and
  two bulk endpoints, and answers the vendor requests of `Common/Drivers/Interface/USB/.tool/common/BenchProtocol.h`. That
  header is shared with the PC benchmark `usbbench`.
- `USBComposite`: a keyboard, a mouse, a serial command line and a vendor loopback channel on one device (`1209:0002`).
- `USBStorage`: a RAM disk with a FAT12 volume on a mass storage device (`1209:0003`). The host mounts it with no formatting.
- `USBDfu`: a DFU 1.1 device (`1209:0004`) whose memory is the top 32 KB of flash, the part the demo image does not use.
  `dfu-util` downloads to it and uploads from it, and the console prints the CRC-32 of each download.
- `USBNetwork`: a CDC-NCM Ethernet adapter (`1209:0005`) that answers ARP, ping and DHCP on `192.168.7.1`, and leases `192.168.7.2`
  to the PC. Plug it in and ping it, with no driver and no configuration.
- `USBAudio`: a USB Audio 1.0 device (`1209:0006`) with a microphone that sends a 1 kHz tone, and a speaker whose audio the
  microphone sends back (a loopback), both at 48 kHz.
- `Regex`: the regex board check. It is not a USB demo. It runs the fixtures of `Application/DemoRegex` against the matcher,
  prints one line per failed check and the stack the matching used over USART1 (`System::console`), and then stays in a loop.
  Build it with `VHAL_REGEX_FIND` in `CMAKE_CXX_FLAGS_RELEASE` (see below) to check `Find` and its stack too.

The demo identities (PIDs, GUIDs, vendor codes and the composite's endpoint map) are in
`Common/Drivers/Interface/USB/.tool/common/DemoProducts.h`, which the PC tools share too. The IDs are pid.codes test values
(`1209:0001` to `1209:0010` are test PIDs): never ship a product with them. Each demo has its own PID, because Windows caches
the descriptors of a VID and PID pair.

The descriptors are checked at compile time against the shared headers: static asserts in
`Application/DemoUSBBench/Descriptors/BenchDescriptors.h`, `Application/DemoUSBComposite/Descriptors/CompositeDescriptors.h`,
`Application/DemoUSBStorage/Descriptors/StorageDescriptors.h`, `Application/DemoUSBDfu/Descriptors/DfuDescriptors.h` and
`Application/DemoUSBNetwork/Descriptors/NetworkDescriptors.h` and `Application/DemoUSBAudio/Descriptors/AudioDescriptors.h`.

## Build

```sh
cmake --preset RelSpeed
cmake --build build/RelSpeed
```

Outputs in `build/RelSpeed/`: `USBBenchG431.elf` and `USBBenchG431.bin` (the `.bin` is written at 0x08000000).
The output is named after the demo. The build needs `arm-none-eabi-gcc` and Ninja on the PATH. Presets: `Debug`, `Release`
(size), `RelSpeed` (speed, use this for the benchmark), `RelDebug`.

To build another demo, give it its own build folder and set `VHAL_DEMO`. Use one build folder per demo: switching
`VHAL_DEMO` in a folder that was built for another demo leaves that demo's `.elf` and `.bin` in it, next to the new ones,
so the folder no longer says which image it holds. The folder names below keep them apart.

```sh
cmake --preset RelSpeed -B build/RelSpeed-USBComposite -DVHAL_DEMO=USBComposite
cmake --build build/RelSpeed-USBComposite
```

`-B` overrides the preset's `binaryDir` (checked with CMake 4.2), so the default build in `build/RelSpeed` stays as it is.
The composite outputs are `build/RelSpeed-USBComposite/USBCompositeG431.elf` and `.bin`.

The storage demo is built the same way:

```sh
cmake --preset RelSpeed -B build/RelSpeed-USBStorage -DVHAL_DEMO=USBStorage
cmake --build build/RelSpeed-USBStorage
```

Its outputs are `build/RelSpeed-USBStorage/USBStorageG431.elf` and `.bin`.

The DFU demo is built the same way:

```sh
cmake --preset RelSpeed -B build/RelSpeed-USBDfu -DVHAL_DEMO=USBDfu
cmake --build build/RelSpeed-USBDfu
```

Its outputs are `build/RelSpeed-USBDfu/USBDfuG431.elf` and `.bin`.

The network demo is built the same way:

```sh
cmake --preset RelSpeed -B build/RelSpeed-USBNetwork -DVHAL_DEMO=USBNetwork
cmake --build build/RelSpeed-USBNetwork
```

Its outputs are `build/RelSpeed-USBNetwork/USBNetworkG431.elf` and `.bin`.

The audio demo is built the same way:

```sh
cmake --preset RelSpeed -B build/RelSpeed-USBAudio -DVHAL_DEMO=USBAudio
cmake --build build/RelSpeed-USBAudio
```

Its outputs are `build/RelSpeed-USBAudio/USBAudioG431.elf` and `.bin`.

The regex board check is built the same way. It has two variants: the default checks `Matches` and `Contains`, and the one
with `VHAL_REGEX_FIND` also checks `Find` and its captures. `vhal.cmake` forces `CMAKE_CXX_FLAGS`, so the define goes in
`CMAKE_CXX_FLAGS_RELEASE`, which the RelSpeed preset uses:

```sh
cmake --preset RelSpeed -B build/RelSpeed-Regex -DVHAL_DEMO=Regex
cmake --build build/RelSpeed-Regex
cmake --preset RelSpeed -B build/RelSpeed-RegexFind -DVHAL_DEMO=Regex -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -DVHAL_REGEX_FIND"
cmake --build build/RelSpeed-RegexFind
```

Its outputs are `build/RelSpeed-Regex/RegexG431.elf` and `.bin`, and the same names in `build/RelSpeed-RegexFind`.

Only the chosen demo folder is compiled and linked (`Application/Application.cpp` and `Application/Application.h` are always
compiled). An unknown `VHAL_DEMO` stops the configure step with the list of demos. The list is the list of
`Application/Demo<Name>` folders.

## Flash

```sh
sh flash.sh
sh flash.sh build/RelSpeed-USBComposite/USBCompositeG431.bin
```

The script writes the image given as its first argument over SWD with STM32CubeProgrammer, verifies it and resets the
board. Without an argument it writes the default bench image, `build/RelSpeed/USBBenchG431.bin`. Set
`STM32_PROGRAMMER_CLI` to use another CLI path, and `STM32_SWD_SN` when more than one ST-LINK is connected.

## Behaviour

All the USB demos share the board setup:

- Clock: HSI16 through the PLL, 170 MHz SYSCLK (range 1 boost, 4 flash wait states). USB 48 MHz from HSI48, trimmed by CRS
  from the USB start-of-frame. SysTick 1 kHz.
- USART1 (PA9 TX, PA10 RX), 115200 8N1. `System::console` is connected to it (`VHAL_SYSTEM_CONSOLE` in `BSP/VHALConfig.h`,
  the write handler in `BSP::Init()`). Its writes never wait: the text goes into a 512-byte ring, and the USART1 transmit
  interrupt sends it in 32-byte chunks through the adapter's asynchronous transmit (`WriteArrayAsync()`, next chunk from the
  `onInterrupt` callback). When the ring is full, characters are dropped and counted in `BSP::consoleDropped` (readable over
  SWD), so the main loop and the USB pump never block on the UART. If USART1 fails to start, no write handler is set and
  nothing is printed; `benchConsoleStatus` (bench) or `compositeConsoleStatus` (composite) holds the result, readable over SWD.
- USB pins PA11/PA12 (alternate function 10). UCPD dead-battery pull-downs are disabled.
- The serial number is the 96-bit MCU unique ID as hex text (`BSP::ReadUniqueId()` reads it for every demo), so one board keeps
  its serial number after a reboot and several boards are told apart on one PC.

### USBBench

- Vendor interface modes (SetMode): Idle, Source, Sink, SourceAndSink, Loopback. ResetCounters, GetCounters, and Reconnect
  (D+ off for the requested milliseconds, then attach again; the main loop does the disconnect, not the interrupt).
- The bulk IN transfers never end with a zero-length packet, because usbbench reads the length it asked for and a zero-length
  packet would end its transfer early. A Source stage is always 2048 bytes. A Loopback stage holds the bytes the host wrote, so
  it can be shorter than 2048 bytes; a count that is not a multiple of 64 ends in a short packet, as usual.
- Every second it prints one line, ending in CR LF, with the USB state, the bench counters, the bus resets and the adapter
  error count.
- `benchCounters` (a volatile global in `Application/DemoUSBBench/DemoUSBBench.cpp`) holds the counters for reading over SWD. Find its
  address in `build/RelSpeed/USBBenchG431.map`, or read it with `STM32_Programmer_CLI -c port=SWD -r <addr> 32`.

The host tool for this demo is `.tool/usbbench`. `usbclasscheck` does not cover it.

### USBComposite

Endpoints, interfaces and the addresses the builder assigns (one endpoint number per endpoint, so IN and OUT never share a
number):

| Interface | Function                  | Endpoints                      | Packet size |
|-----------|---------------------------|--------------------------------|-------------|
| 0         | Keyboard (boot)           | interrupt IN `0x81`            | 8           |
| 1         | Mouse (boot)              | interrupt IN `0x82`            | 4           |
| 2         | Serial port, control      | notification interrupt IN `0x83` | 16        |
| 3         | Serial port, data         | bulk IN `0x84`, bulk OUT `0x05` | 64 each    |
| 4         | Vendor raw channel        | bulk IN `0x86`, bulk OUT `0x07` | 64 each    |

That is seven endpoints, numbers 1 to 7 (`EndpointNumberLimit(7)`), with endpoint 0 on top. The port is opened with
`endpointCount` 8, because the default of 4 only covers numbers 0 to 3.

Identities: VID `1209`, PID `0002`. The vendor channel binds WinUSB through the Microsoft OS 2.0 descriptors, with the
interface GUID `{53B72DEF-1477-422A-B645-D5267E3E44A6}`. The MS OS 2.0 vendor code is `0x21` (the bench uses `0x20`). The
composite differs only so that a request meant for one demo is never answered by the other one's descriptors, in a trace or
by a PC tool that talks to both.

Packet memory (PMA), 1024 bytes on the G431: the buffer table takes 64 bytes, endpoint 0 takes 128, the four bulk endpoints
are double-buffered (4 x 128 = 512), and the three interrupt endpoints take 8 + 4 + 16 = 28. That is 732 bytes, with 292 bytes
to spare. This is the adapter's allocation rule worked out, not a measurement on a board.

Build size: RAM 10200 B of 22 KB (45 %), FLASH 96484 B of 128 KB (74 %). The bench is 16040 B RAM and 78364 B FLASH.
`CdcPrint::WriteRaw` is defined in `CdcPrint.cpp`: inlined at every print site at -O3 it made the image about 29 KB larger.

The CDC port is a command line. Echo, backspace (BS or DEL), and a line end ends a line: CR (what a terminal sends for Enter),
LF, or CR LF, and each of those is one end. The second byte of a CR LF or LF CR pair ends nothing, while two CRs (or two LFs)
end two lines. A line of spaces only is an empty line and gets a new prompt. A prompt follows each command. Each command's text or name has a
length limit (`type` 75 characters, or 73 between double quotes; a key name and a button name 16), and a longer one is refused. Ctrl+C cancels the line
being typed and any typing in progress, and prints `^C`. An escape sequence, such as an arrow key, is ignored, and so is the character right after its ESC. Commands:

- `help`: the list.
- `type <text>`: types the text on a US keyboard layout, printable ASCII only. A text with spaces goes in double quotes, as in
  `type "hello world"`; inside the quotes `\"` is a double quote and `\\` a backslash. The text is typed a few characters per
  pass as the keyboard queue has room. The prompt comes after the last character is typed. Input typed meanwhile is kept in a 128-byte ring; the rest is lost, and the console prints `input lost`.
- `key <name>`: presses and releases one key. Names: `enter`, `esc`, `backspace`, `tab`, `space`, `up`, `down`, `left`,
  `right`, `f13` to `f24`. The function keys are keyboard page usages 0x68 to 0x73, so the host gets them as F13 to F24 (a
  program can bind them; a keyboard shows no keys for them).
- `move <dx> <dy>`: moves the mouse. Counts are signed; positive is right and down. Values from -32767 to 32767.
- `click [left|right|middle]`: clicks a button, left by default.
- `wheel <n>`: scrolls n steps, positive away from you.
- `status`: USB state and address, keyboard LEDs (Num, Caps, Scroll Lock), line coding, DTR, the number of vendor bytes
  looped back, and the CDC bytes dropped.

Commands that drive the keyboard or the mouse answer `error: the USB host has not configured the device yet` until the
host has configured the device. A `move` or `wheel` the queue has no room for waits in the mouse and goes out from the main
loop, and it still answers `ok`. A movement that would take an axis past 32767 pending steps is refused with an error. The
pending movement is dropped when the host deconfigures or resets the device, so the next enumeration does not get it as one
jump. The serial port writes only while a terminal is open (DTR set). When the transmit ring is
full, the rest of the text is dropped and counted in `cdcDropped`, so the main loop never waits for the host.

The vendor raw channel loops every received byte back. The once-a-second line on USART1 shows the USB state, DTR, the
looped bytes, the CDC bytes dropped and the USB error count.

Try it on Windows:

1. Flash the composite image. Windows binds the inbox HID keyboard and mouse drivers, the inbox serial driver (a COM port
   named `USB Serial Device`) and WinUSB for the vendor channel.
2. Open the COM port in any terminal program (the baud rate is not used by the device), then type `help`.
3. The keyboard and the mouse act on the focused window. Click into Notepad first, then send `type hello` or `key enter`.
   `move` and `click` move and click wherever the pointer is.
4. The vendor channel is on interface 4, with WinUSB bound through the interface GUID above. A WinUSB program sends bytes on
   `0x07` and receives them back on `0x86`. The PC check is `usbclasscheck composite` (`.tool/usbclasscheck`): the serial port, the keyboard and mouse, and the vendor loopback.
`usbclasscheck composite hid` presses F13 to F24 and moves the pointer briefly. The check saves the pointer position before the moves
and puts it back afterwards, even when a move fails, so the pointer ends where it started; no button is clicked.

### USBStorage

The mass storage device (`1209:0003`) has one Bulk-Only interface of the in-box mass storage class, so Windows, Linux and macOS
bind it with no driver. The host sees a removable disk, labelled `VHAL DEMO`, of 28 blocks of 512 bytes (14336 bytes). The disk
is filled from a FAT12 volume that is built at compile time (`Application/DemoUSBStorage/Disk/StorageVolume.h`). The volume has
`README.TXT`, which says what the device is, and `INFO.TXT` with the IDs, so the host mounts it with no formatting.

- Identity: vendor `VHAL`, product `RAM Disk Demo`, revision `1.00` (the device release). The serial number is the MCU unique ID as
  24 hex characters. The mass storage class asks for at least 12 hex characters on a Bulk-Only device, and the ID text meets that.
- The disk is writable and lives in RAM. Files written to it stay until the board is reset or loses power, and a reset brings the
  image back.
- Eject ("Safely Remove" on Windows) takes the disk away. The disk comes back with the next configuration: unplug the board and
  plug it in again, and the host finds the disk with its files, as with a USB stick. The disk does not come back by itself
  between an eject and a configuration.
- The status line, once a second: `t`, the USB state, the address, `medium` (present or absent), `blocksRead` and `blocksWritten`
  (totals since reset) and `usbErr`. The mass storage function does not expose the sense data of its last error, so the line has no
  sense field.
- Build size: RAM 20016 B of 22 KB, with the 1 KB stack block counted (the `.data` and `.bss` are 18992 B: the disk 14336 B, the mass
  storage buffer 2048 B and the rest 2608 B), leaving 2512 B free. FLASH 90080 B of 128 KB (69 %). The disk is 28 blocks, because 29
  would leave less than the 2 KB margin. The mass storage buffer is 2048 bytes, so a transfer moves up to 2 KB per main loop pass.

Try it on Windows:

1. Flash the image: `sh flash.sh build/RelSpeed-USBStorage/USBStorageG431.bin`.
2. Plug the board in. A drive `VHAL DEMO` appears. Open `README.TXT`, then create or copy files and read them back.
3. Eject it with "Safely Remove". Unplug the board and plug it in again: the disk comes back with its files.

The PC check is `usbclasscheck storage` (`.tool/usbclasscheck`). It writes only to the volume whose disk is on this board (the USB
device node above the disk has this board's serial number). It writes a test file to the volume, reads it back without the
cache and deletes it.

### USBDfu

A DFU 1.1 device (`1209:0004`) in DFU mode. Its memory is one region, the top 32 KB of flash, `0x08018000` to `0x0801FFFF`
(16 pages of 2 KB). The demo image ends well below it. The interface string names the region:
`Upper flash 0x08018000 32 KB`. The device stays on the bus after a download (manifestation tolerant), and it does not restart
into the new image: the demo only fills the region.

- Identity: the PID, the interface GUID and the MS OS 2.0 vendor code `0x22` are in `DemoProducts.h`. WinUSB is bound through
  the MS OS 2.0 descriptors, so dfu-util opens the device on Windows with no Zadig.
- Transfers: blocks of up to 2048 bytes (`wTransferSize`). A download writes from offset 0 and must fit in the 32 KB region. An
  upload reads from offset 0 to the end of the region, so it returns 32768 bytes.
- Overlap check: the image must end below `0x08018000`. The linker checks it: an `ASSERT` in `STM32G431XB_FLASH.ld` compares the
  end of the loaded image with `VhalDfuRegionStart`, which CMake defines for this demo only. A build whose image grows into the
  region fails with a message. The start and size are `VHAL_DFU_REGION_START` and `VHAL_DFU_REGION_SIZE` in `CMakeLists.txt`.
  If you change them, change the interface name in `Application/DemoUSBDfu/Descriptors/DfuDescriptors.h` too, and the values
  `DfuRegionStart` and `DfuRegionSize` in `Common/Drivers/Interface/USB/.tool/common/DemoProducts.h`. `Target/DfuRegionTarget.h`
  asserts that they match the CMake values, and the PC tool reads them.
- CRC: when a download completes, the console prints `download complete bytes=N crc32=0x........` once, and the status line
  shows the same as `last=`. The CRC is the IEEE CRC-32 (the zlib one) of the N bytes from `0x08018000`, which are the bytes of
  the file, so it equals the CRC-32 of the file. It is computed in the main loop, not in an interrupt. To check it on the PC:
  `python -c "import sys,zlib; print('0x%08X' % zlib.crc32(open(sys.argv[1],'rb').read()))" file.bin`.
- The status line, once a second: `t`, `usb` (the USB state), `addr`, `dfu` (the DFU state), `status` (the DFU status),
  `downloaded` and `uploaded` (bytes since start), and `last` (bytes and CRC of the last completed download, or `none`).
- Poll timeout: the host waits 406 ms between status requests while a block is written, and one block is one 2 KB page. The value
  is built from assumed times, not measured ones: 100 ms for the page erase (the FLASH adapter's own bound), 1 ms for each double
  word with its read-back, and 50 ms for the blank check. The constants are in `Target/DfuRegionTarget.h`. The times are to be
  measured on the board. A block that takes longer is still answered busy, so the host polls again.
- USB during a write (to be confirmed on the board): the write runs from flash in the main loop. On the single-bank G431 the CPU
  stalls while a page is erased or programmed, and the interrupts wait with it, so the USB interrupt waits during a page erase.
  The host's control requests are NAKed meanwhile and complete afterwards. Not measured yet.
- Flashing the demo with `flash.sh` writes the image without a chip erase, so the region keeps what it held. A programmer
  option that erases the whole chip clears it.
- Build size: RAM 5656 B of 22 KB (25 %), FLASH 75372 B of 128 KB (58 %).

Try it on Windows:

1. Build and flash the image: `sh flash.sh build/RelSpeed-USBDfu/USBDfuG431.bin`. The board enumerates as `USB DFU` with WinUSB
   bound, so no driver step is needed.
2. `dfu-util -l` lists the region as alternate 0: `Upper flash 0x08018000 32 KB`.
3. `dfu-util -d 1209:0004 -a 0 -D file.bin` downloads a file. dfu-util warns that the file has no DFU suffix; the device does not
   check the suffix. The console then prints the byte count and the CRC-32 of the download. Compare it with the CRC-32 of
   `file.bin` from the command above.
4. `dfu-util -d 1209:0004 -a 0 -U back.bin` uploads the region to a file.

The PC check is `usbclasscheck dfu --console COMn` (`.tool/usbclasscheck`). It does steps 3 and 4 with a random file, compares
them, and checks the CRC line on the console. Like step 3, it overwrites the region.

On Linux the same commands work with libusb and no driver. Non-root access needs a udev rule, which this stack does not provide.

### USBNetwork

A CDC-NCM Ethernet device (`1209:0005`). The PC sees a network adapter, not a serial port. The board answers IPv4 by itself, and
the PC needs no configuration.

- Identity: PID `0x0005` (`DemoProducts::NetworkProductId`), manufacturer `VHAL`, product `NCM Network Demo`. The NCM class codes
  bind the in-box drivers of Windows 10 2004 and later, Linux (`cdc_ncm`) and macOS, so there is no INF, no WinUSB and no Zadig.
- Addresses: the board is `192.168.7.1` (ARP and echo answer on it). Its DHCP server leases `192.168.7.2` to the PC for one hour,
  with the netmask `255.255.255.0`. The lease has no router and no DNS server. The values are in `DemoProducts.h`.
- MAC addresses: derived from the 12-byte unique ID, the bytes the serial number is the hex text of. The host's own adapter takes
  bytes 0 to 5 (`iMACAddress`, the run-time string), the board takes bytes 6 to 11. Both are locally administered unicast, and
  the rule is `DemoProducts::NetworkMacsFromUniqueId()`, so a PC tool computes the same two MACs from the serial number.
- The responder (`Network/`): pure functions over byte spans, called from the main loop.
  - ARP: a request for `192.168.7.1` gets a reply. A probe (sender `0.0.0.0`) and a request for another address get nothing.
  - ICMP: an echo request to `192.168.7.1` gets an echo reply with the same identifier, sequence and payload, of any size the frame
    allows (up to 1514 bytes).
  - DHCP (BOOTP over UDP 67 and 68): a DISCOVER gets an OFFER of `192.168.7.2`. A REQUEST for that address (option 50, or `ciaddr`
    when the PC renews) gets an ACK, sent to the broadcast address, or to the PC's address and MAC when the PC asked for a unicast
    reply. A REQUEST for another address or another server gets nothing.
  - Everything else is dropped and counted. The checks are: the lengths of every header, the IPv4 header checksum, the ICMP checksum,
    the UDP checksum when the sender set one, the DHCP cookie and options, and no fragments. A frame that breaks one of them is
    malformed and is dropped. A frame that is valid but not for the board is dropped. No frame is read past its end.
  - Not implemented: IPv6, TCP, other UDP services, other ICMP types, routing, the DHCP NAK, RELEASE and INFORM messages, and ARP
    requests by the board. The responder keeps no state except the counters, because it has one address to lease.
  - DHCP option overload (option 52, which continues the options in the `sname` and `file` fields) is not supported. The parser
    reads the options field only, so a client that puts its message type or requested address in an overloaded field gets no answer.
- The status line, once a second: `t` (ms), `usb` (the USB state), `data` (`active` while the host has selected the data
  interface's alternate 1), `link` (`up` or `down`), `in` and `out` (frames the NCM function received and packed into NTBs), `arp`,
  `icmp`, `dhcpOffer` and `dhcpAck` (answers sent), `dropped` (frames with no answer), `malformed` (the part of `dropped` that broke a
  rule), `rxDropped` (frames the receive queue refused), `txRefused` (answers the transmit queue refused: no data path, or the queue
  was full) and `usbErr`.
- Sizes: the NCM function is `NcmFunction<2048, 2048, 3, 2>`: 2048-byte NTBs in both directions (the spec's minimum), three frames
  queued for the host and two received frames. The demo keeps one receive and one reply frame of 1514 bytes each.
  RAM 19064 B of 22 KB (85 %), counted with the 1024-byte stack block, so 3464 B are free. Of that, 4096 B are the NTB buffers, 4560 B
  the transmit queue (3 frames of 1520 B), 3040 B the receive queue (2 frames), 3028 B the two frame buffers, and 1024 B the stack. The
  rest, 3316 B, is the console ring (544 B with its transmit chunk) and everything else the image keeps in RAM, not itemised here. With four transmit frames the image would take 20584 B,
  which leaves 1944 B free, under the 2 KB margin, so three is the depth. FLASH 77248 B of 128 KB (59 %). These are the linker's
  figures, not a measurement on a board.

Try it on Windows 11:

1. Flash the image: `sh flash.sh build/RelSpeed-USBNetwork/USBNetworkG431.bin`.
2. Plug the board in. An adapter named for the NCM device (`Ethernet over USB`) appears in the network connections, with no driver
   prompt. Within a few seconds it gets `192.168.7.2` from the board (`ipconfig`). Windows may show the network as "Unidentified
   network", because the board is not a gateway to anything.
3. `ping 192.168.7.1` gets replies. The console's `arp`, `icmp` and `dhcpAck` counts go up.
4. The PC check is `usbclasscheck network` (`.tool/usbclasscheck`). It finds the adapter by the MAC derived from the serial number,
   waits for the lease, pings through the leased address with four payload sizes, and reads the ARP entry. It changes nothing on the PC.

On Linux the same steps work with the kernel's `cdc_ncm` driver: the new interface (`ip link`) takes `192.168.7.2` from the
system's DHCP client, NetworkManager or `dhclient`, and `ping 192.168.7.1` gets replies.

The host tests for this code are `.tool/sim/TestNetworkDemo.cpp` (ARP, ICMP with hand-computed checksums, DHCP DISCOVER, REQUEST,
renewal and malformed input, written by hand from the RFCs) and the `TestNcm*` tests for the NCM function. The board has not run
it yet: the flash, the Windows and Linux steps are still to be tried on hardware.

### USBAudio

A USB Audio 1.0 device (`1209:0006`): one Audio Control interface and two AudioStreaming interfaces, a microphone and a speaker. The
PC sees a recording device and a playback device, with no driver and no configuration.

- Identity: PID `0x0006` (`DemoProducts::AudioProductId`), manufacturer `VHAL`, product `VHAL Audio Demo`. The audio class codes bind
  the in-box drivers (Windows `usbaudio.sys`, Linux `snd-usb-audio`, macOS), so there is no INF and no WinUSB. The product string
  contains `VHAL`; the name Windows shows for the recording device is not yet confirmed on a board.
- Microphone: mono, 48 kHz, isochronous IN `0x81`, synchronous (`bmAttributes` 0x0D), with volume and mute. Each packet holds 48 samples
  (96 bytes, one millisecond of frames) in a 98-byte endpoint. The first packet is armed when the host selects alternate 1, before the
  application can write, so it is silence; each completion arms the next one. No start-of-frame event is used. A ring that runs short
  pads the packet with silence and counts an underrun (`micUnder`).
  The microphone sends a 1 kHz sine of 8000 LSB peak (`DemoProducts::AudioToneFrequency`, `AudioToneAmplitude`) while the loopback is
  off. A speaker packet with a sample above 64 LSB in magnitude (about -54 dBFS) starts the loopback; silence and dither do not. The
  threshold is well above dither: Windows keeps an open speaker at -1, 0 and +1 while nothing plays (measured on Windows 11), and a
  threshold of zero let that dither start the loopback, so the microphone sent the dither instead of the tone. The tool's loopback tone
  is 0.25 of full scale (8192 LSB), far above the threshold. The threshold is checked on the speaker's samples before its volume is
  applied. The loopback holds for 100 ms after the speaker's last sound. During the hold, a frame the speaker has not sent is silence, so a short gap does not count as
  an underrun. When the hold ends, the tone returns. The two channels are averaged to one, so a mono signal sent on both channels comes
  back unchanged. The microphone's volume and mute scale the tone. The microphone's mute also silences the loopback, but its volume
  does not scale the loopback.
- Speaker: stereo, 48 kHz, isochronous OUT `0x02`, adaptive (`bmAttributes` 0x09), with volume and mute. The host sends 96 samples
  (192 bytes, 48 stereo frames) each millisecond, in a 196-byte endpoint: the endpoint's maximum takes one frame more than a
  millisecond, as the rate allows. Each completion copies the packet into the ring and arms the next one. The volume and mute of the
  speaker scale the loopback. A pass of the main loop reads the speaker's ring on every pass, also while the microphone is idle, so the
  ring does not overflow. With the microphone streaming, a pass fills the microphone's ring until it is full. A pass moves at most eight
  chunks of 16 frames, so the main loop returns to `usbDevice.Process()` often enough for the host's packets.
- While the microphone's ring is full, a pass cannot write, and it used to return without reading the speaker. The speaker keeps
  delivering a packet each millisecond, so its backlog grew. Now such a pass drops the speaker's samples above two packets (192
  samples) and counts them as `spkDrop`. A microphone that takes its packets should keep the backlog at one or two packets, so a
  normal loopback drops nothing; the status line shows whether it does. These samples are dropped on purpose, so they are not overruns
  (`spkOver`). If the microphone stops in the middle of a pass, the speaker samples that pass has already read are dropped too, and
  they are counted in `spkDrop` as well.
- Volumes are in 1/256 dB, 0 dB (unity) to -60 dB (-15360). The gains are computed by the compiler (`Audio/AudioSignal.h`), with no
  floating-point work at run time. The tone is a 48-sample table, one period of 1 kHz at 48 kHz.
- Only one sampling rate, so the endpoints have no sampling frequency control, and a SET_CUR to another rate is stalled.
- Packet memory (the G4 port): the buffer table takes 24 bytes, because the endpoints are 0x81 and 0x02 (numbers 0 to 2), and endpoint 0
  takes 128, which leaves 872 bytes. The microphone's 98-byte packet takes two 98-byte IN blocks, 196 bytes; the speaker's 196-byte packet
  takes two 224-byte OUT blocks, 448 bytes. Together 644 of 872 bytes, 796 of 1024 in all. A stereo microphone's 196-byte IN blocks would
  take 392 bytes, and 840 of 872 beside this speaker, so it fits too.
- RAM 5648 B of 22 KB (25.07 %), and FLASH 81996 B of 128 KB (62.56 %), from the linker of the RelSpeed build with
  `VHAL_DEMO=USBAudio` (`cmake --preset RelSpeed -B <dir> -DVHAL_DEMO=USBAudio`). Of the RAM, the audio function keeps two rings (192
  microphone samples and 384 speaker samples, 1152 B) and its packet buffers, about 0.8 KB.
- The status line, once a second: `t` (ms), `usb` (the USB state), `mic` and `spk` (`active` while the host has selected the
  stream's alternate 1), `rate`, `micVol`, `micMute`, `spkVol` and `spkMute` (the volumes in 1/256 dB, the mutes as 0 or 1),
  `micPkts` (microphone packets armed for the host; one the host misses still counts), `micUnder` (microphone packets padded with
  silence: real underruns, since the hold fills the ring with silence), `micRefused` (packets the port refused to arm; the microphone
  then stays idle until the next SET_INTERFACE), `spkPkts` (speaker packets received), `spkOver` (speaker samples the ring refused
  because it was full), `spkDrop` (speaker samples the demo dropped: the backlog dropped on purpose, see above, and the samples a pass had
  read when the microphone stopped under it; the speaker samples read and thrown away while the microphone is idle are not counted),
  `loopback` (`on` while the microphone sends
  the speaker's audio, the hold included) and `usbErr`. The counters run from the start and a bus reset keeps them.

The second hardware run (Windows 11, the board attached, status lines over 60 seconds) showed:

- `loopback` was `on` for the whole run. Host applications were playing to the board's speaker, because Windows had made the board
  the default output device, so real sound was looped back. The 64 LSB threshold kept the loopback on for that sound.
- `spkOver` stayed 0. `spkDrop` went to 384 once at the stream start and stayed constant after it.
- `micUnder` was 1.
- `micVol` went from 0 to -768 (-3 dB) during the run. A host application set it.
- A host application opened the microphone before the check ran, because Windows had also made the board the default input.

`spkOver` at the start of a microphone stream. On the first hardware run `spkOver` grew by 96 (one packet) each time the microphone
stream started, and not during a recording. The pump reads the speaker only in a pass that writes to the microphone, and at a start the
microphone's ring is full, so the speaker's packets piled up until the ring refused one. The pump's discard, added after that run,
drops the backlog above two packets, so the ring no longer refuses a packet at a start. On the second hardware run `spkOver` stayed 0
throughout, and `spkDrop` went to 384 once at the stream start and stayed there: the discard ran once and dropped the start backlog.
The `spkOver` at a start came from the pump path, and the discard removes it.

Try it on Windows 11:

1. Flash the image: `sh flash.sh build/RelSpeed-USBAudio/USBAudioG431.bin`.
2. Plug the board in. A recording device and a playback device with `VHAL` in the name appear in the sound settings. Windows may make
   the board its default output device and its default input device, so the PC's sound plays through the board, and host
   applications record from its microphone, until you pick other devices in the sound settings. While the board is attached, set
   the PC's own speaker and microphone back as the defaults; Windows then remembers the choice.
3. Record from the microphone for a few seconds. The recording is a 1 kHz tone, and `micPkts` goes up.
4. Play a sound into the speaker and record the microphone again. The recording is the speaker's sound, and `loopback` shows `on`
   while the speaker plays. Stop the playback and the tone comes back within a tenth of a second.
5. Change the microphone's volume or mute in the sound settings: the tone follows it. The microphone's mute also silences the loopback,
   but its volume does not change the loopback. The speaker's volume and mute scale the loopback.
6. The PC check is `usbclasscheck audio` (`.tool/usbclasscheck`). It finds the board's microphone and speaker in Windows audio by
   their USB device, records the 1 kHz tone, then plays a 440 Hz tone into the board's speaker and checks that the recording switches
   to it. Only the board's two endpoints are used, and the default devices and volumes are not changed. The tone is audible on the
   board's speaker while the check runs.

The first hardware run (Windows 11, the board attached to the PC) showed:

- Both directions run at 1000 packets per second: the microphone's `micPkts` and the speaker's `spkPkts` each grow by 1000 a second.
- While Windows kept the board's speaker open with nothing playing, the speaker received only -1, 0 and +1 (Windows dithers digital
  silence). The loopback took them for sound, so the microphone sent that dither instead of the tone: a recording of 190752 samples
  held only the values -1, 0 and +1. The threshold above (64 LSB) fixes this: the tone returns once the speaker has been silent for
  the 100 ms hold.
- `spkOver` grew by 96 each time the microphone stream started (96 to 192 at the start of a recording), and not during a recording.
  See the section above for what is and is not known about it.
- Windows may make the board its default output device, so the PC's sound plays through the board until the user switches the
  default back.

The host tests for the signal are `.tool/sim/TestAudioDemo.cpp` (the tone table against `std::sin`, the gain table, the volume and
mute scaling, the loopback downmix, the loopback's signal threshold at 64 and 65 LSB, the speaker backlog and the tone phase). The descriptors and the audio function are tested
by the `TestAudio*` tests in `.tool/sim`: the sync types and packet sizes, the first packet armed at alternate 1, the arming after each
completion, the refused arms, the requests and the rate change. `DemoUSBAudio.cpp` itself is not run on the host; the build checks it.

Verified on the host: the items above, and the signal's threshold and backlog rules. The first and second hardware runs are listed above.
The second run measured the threshold (the loopback held on real sound), the `spkDrop` discard and `spkOver` at a start. Not verified
on hardware yet: the Linux steps, the name Windows shows, and the packet timing beyond the 1000 packets a second. The RAM and FLASH
figures above are the linker's, not a measurement.

## Layout

- `BSP/`: clocks, USART1 and the console ring (`System::console` write handler and transmit chunks), USB clock, interrupt
  vectors, `VHALConfig.h`, and `BSP::ReadUniqueId()`.
- `Application/`: `Application.h/.cpp` starts the demo the build selected. The `#if` on `VHAL_DEMO_<name>` picks it;
  `main.cpp` does not change between demos.
  - `Application/DemoUSBBench/`: the USB benchmark device. `DemoUSBBench.h/.cpp` has the USB device start, the main loop
    with the USB pump, the Reconnect handling and the once-a-second status line. Its two domains sit in their own folders:
    `Descriptors/BenchDescriptors.h` has the descriptors, `Function/BenchFunction.h` the bench function.
  - `Application/DemoUSBComposite/`: the composite device. `DemoUSBComposite.h/.cpp` has the device, the functions, the main
    loop, the vendor loopback and the status line. `Descriptors/CompositeDescriptors.h` has the descriptors and their asserts,
    `Function/` the function types and the serial print, `Status/` the status text shared by the status line and the
    `status` command, and `CommandLine/` the command line.
  - `Application/DemoUSBStorage/`: the mass storage device. `DemoUSBStorage.h/.cpp` has the device, the main loop and the status
    line. `Descriptors/StorageDescriptors.h` has the descriptors and their asserts, and `Disk/` the RAM disk's volume
    (`StorageVolume.h`, the FAT12 image and the block count) and the block counter (`CountingBlockDevice.h`).
  - `Application/DemoUSBDfu/`: the DFU device. `DemoUSBDfu.h/.cpp` has the device, the main loop, the manifest CRC and the status
    line. `Descriptors/DfuDescriptors.h` has the descriptors and their asserts, `Target/DfuRegionTarget.h` the memory target over
    the region (the library's flash target with the byte counts), and `Status/DfuStatusText.h` the state and status names.
  - `Application/DemoUSBNetwork/`: the network device. `DemoUSBNetwork.h/.cpp` has the device, the main loop, the responder call and
    the status line. `Descriptors/NetworkDescriptors.h` has the descriptors and their asserts. `Network/` has the protocol code as pure
    functions over byte spans: `NetworkBytes.h` (big-endian fields), `InternetChecksum.h`, `EthernetFrame.h`, `Arp.h`,
    `Ipv4Packet.h`, `IcmpEcho.h`, `UdpPacket.h`, `Dhcp.h` and `NetworkResponder.h`, which answers one frame. The host test
    `.tool/sim/TestNetworkDemo.cpp` includes them.
  - `Application/DemoUSBAudio/`: the audio device. `DemoUSBAudio.h/.cpp` has the device, the main loop with the microphone and
    speaker pass (the tone, the loopback and the volume scaling), and the status line. `Descriptors/AudioDescriptors.h` has the
    descriptors and their asserts. `Audio/AudioSignal.h` has the tone table, the gain table, the volume scaling and the downmix as
    pure functions, which `.tool/sim/TestAudioDemo.cpp` includes.
  - A new demo goes in its own folder `Application/Demo<Name>/`, with a branch in `Application/Application.cpp`. The folder
    makes `VHAL_DEMO=<Name>` valid. Its PID and GUID go in `DemoProducts.h`.
- `STM32G431XB_FLASH.ld`: linker script (128 KB flash, 22 KB RAM: SRAM1 16 KB plus SRAM2 6 KB, contiguous; the 10 KB CCM
  SRAM is not used).
- `arm-none-eabi.cmake`, `CMakePresets.json`, `CMakeLists.txt`: build on `vhal.cmake`. `CMakeLists.txt` finds VHAL two
  levels up (`.demo/STM32G4/../..`), lists the demos from the folders, and includes the shared headers from
  `Common/Drivers/Interface/USB/.tool/common` (`BenchProtocol.h`, `DemoProducts.h`).
