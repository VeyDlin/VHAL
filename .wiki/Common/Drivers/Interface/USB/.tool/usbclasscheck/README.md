# usbclasscheck

Checks the USB classes of the STM32G4 demo boards (`.demo/STM32G4`) end to end, on a real board. For each check it prints a
PASS or FAIL line per step, and it exits non-zero when a check fails. It covers the five demos that have a class check:
`USBComposite` (serial port, keyboard, mouse and vendor channel), `USBStorage` (the disk), `USBDfu` (the download and
upload), `USBNetwork` (the adapter, the DHCP lease, the echoes and the ARP entry) and `USBAudio` (the microphone's tone, its
level and the speaker's loopback into the microphone). `USBBench` is measured with `.tool/usbbench` instead.

The identities (PIDs, GUIDs, the vendor codes, the DFU region, the audio tone) come from `Common/Drivers/Interface/USB/.tool/common/DemoProducts.h`, which the
firmware also includes. The protocol of the command line is the one `CompositeCommandLine.cpp` implements.

Status: the offline tests run without a board. The hardware checks have been run on one board (Windows 11, STM32G431). The round-four
tool, before the review fixes of round five, gave these results:

- `composite` `cdc`: passed.
- `composite` `vendor`: the round trips of 1, 63, 64, 65, 4096 and 65536 bytes passed.
- `dfu`: passed for the list, the download, the CRC-32, the upload, the compare and the console CRC line.
- `audio`: not run with the round-four tool.

The round-five fixes are not yet run on a board: the process runner checks its deadline on every pass, the vendor round trip ends a
request that does not complete with a bounded wait and a cancel (and keeps the memory of a request that still does not end), the dfu
upload file is made in a folder of its own, created for the run, and only the file dfu-util creates there is deleted, the volume
label is compared as UTF-8, and the microphone drain stops at its deadline. The audio changes of the same round were run on a board
with the current build: audio passed, with raw streams on the microphone and the speaker, and both recordings had the start flag on
the first packet after Start and no other discontinuity. Run the checks again before relying on these, and treat a first failure
as a question about the check as well as about the board.

## Build

Windows, MinGW-w64 g++ 15 (C++20), CMake and Ninja. Only Win32 APIs and the libraries that Windows ships are used: SetupAPI,
the configuration manager, WinUSB, Raw Input, the IP Helper (adapter and neighbour tables, ICMP) and the audio endpoints (the MMDevice
API, Core Audio, through COM). There is no third-party code.

Run the commands from the `.tool` folder (`Common/Drivers/Interface/USB/.tool`). The paths are relative to it, and the `build` folder is made there.

```
cmake -S usbclasscheck -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build
```

The build compiles with `-Wall -Wextra -Wpedantic`, and it gives no warnings. The tests are the pure logic only: they need no
board and no Windows device. The IP Helper calls (neighbour table, adapter table, ICMP) exist from Windows Vista on. The target is Windows 10, which is set
in `CMakeLists.txt` (`_WIN32_WINNT` and `NTDDI_VERSION`).

## Usage

```
usbclasscheck list                          every attached demo board with its PID, demo name and serial number
usbclasscheck composite [cdc|hid|vendor]    USBComposite: serial port, keyboard and mouse, vendor loopback
usbclasscheck storage                       USBStorage: the VHAL DEMO volume, its files and a write and read back
usbclasscheck dfu                           USBDfu: a download and an upload through dfu-util, compared byte for byte
usbclasscheck network                       USBNetwork: the host's adapter, the DHCP lease, echoes and the ARP entry
usbclasscheck audio                         USBAudio: the microphone's tone and level, and the speaker's loopback
```

Options:

- `--serial text`: the board with this serial number, as `list` prints it. The match ignores ASCII case.
- `--dfu-util path`: the dfu-util program (dfu only). Without it, dfu-util is looked up on the PATH.
- `--console COMn`: the board's console at 115200 8N1 (dfu only). The check reads its `download complete ... crc32=0x........`
  line while the transfer runs, and compares the CRC with the file's.
- `--help`: the usage text.

With one board of a demo attached, the command works without `--serial`. With several boards of the demo attached and no
`--serial`, the command lists them and stops (exit 2). It does not guess, because a run on the wrong board still prints a result.

Exit codes: `0` all checks pass, `1` a check could not run (no board, no COM port, the device did not answer, a tool is
missing), `2` usage error, `3` a check ran and failed. If one check could not run and another failed, the exit code is 1.

Example of the output format (the values are placeholders, not a result):

```
  PASS  COM port: COM<n>, DTR raised
  FAIL  line coding 9600 7E1: the device reports 9600 8N1
cdc: FAIL

Result: FAIL: cdc
```

## What each check does

### composite

The three checks run in order unless one is named. Each one finds the board's own interface, so a second composite on the
bus is not used by mistake.

- `cdc`: finds the COM port of the serial interface (interface 2, the CDC-ACM control interface) through SetupAPI: the device
  of the Ports class whose instance belongs to this board's VID, PID and serial. It opens the port, raises DTR (the demo
  prints only while DTR is set), and sets 57600 8N1 and then 9600 7E1 with `SetCommState`. After each setting it sends `status`
  and checks the line coding the device reports, and DTR. It checks that the command is echoed and the prompt follows the
  answer, and that `nosuchcommand` gets the `error: unknown command` line.
  - Changes on the PC: the port is opened and its line coding is set. The coding is left as the last setting (9600 7E1) when
    the check ends. The demo ignores the rate, so this is only the port's own state.
- `hid`: needs the board configured (`state=Configured` in `status`). The keyboard and mouse input are read through Raw Input in
  a message-only window registered with `RIDEV_INPUTSINK`, so focus does not matter. Only events from this board are kept: the
  device of each event is traced up the device tree to the USB device node, and its VID, PID and serial are compared. The
  check sends `key f13` to `key f24` and expects a make and a break of VK_F13 to VK_F24 from this board, in order. Then it sends
  `move 25 -10`, `move -25 10`, `wheel 1` and `wheel -1`, and expects the movement from this board to net to zero (step
  `motion nets to zero`): the positive and negative counts of each axis must match the moves sent, and the wheel must be seen
  both ways.
  - Changes on the PC: the keys F13 to F24 reach the focused window (most applications ignore them). The pointer position is
    saved with GetCursorPos before the first move command, and it is put back with SetCursorPos after the last one, whatever
    the outcome of the check (step `pointer put back`). So the pointer ends where it started, even though pointer acceleration
    means the screen movement is not the raw counts. If the position cannot be read, no move is sent (step `pointer position`).
    If Windows refuses the put-back, the step is an error and the pointer may be elsewhere. No button is clicked.
- `vendor`: opens the vendor raw channel (interface 4, MI_04) with WinUSB through the composite's interface GUID from
  `DemoProducts.h`. It checks that the bulk endpoints 0x86 IN and 0x07 OUT are there. Then it sends random blocks of 1, 63, 64,
  65, 4096 and 65536 bytes, reads the loopback at the same time, compares the bytes, and prints the rate in MB/s (10^6 bytes
  per second).
  - Why the read loops: the device does not know the host's read size, so it ends an answer that is a whole number of packets
    with a zero-length packet. With one-packet OUT transfers an echo can also come back as several IN completions (for 65 bytes:
    64, a zero-length packet, then 1). The IN side therefore reads until the block has all its bytes. Each read asks for the rest
    of the block, a zero-length or short completion makes it read again, and more bytes than the block is an error. The write stays
    one request. The round trip has one 10 s deadline for all of its reads. The accumulation is pure code in `TransferLogic.*`,
    and its test runs without a board.
  - Changes on the PC: none beyond the open WinUSB handle, which is closed at the end.

### storage

Finds the mounted volume labelled `VHAL DEMO` whose disk is this board's. The chain is read from the volume to the board: the
volume's disk number (`IOCTL_STORAGE_GET_DEVICE_NUMBER`) gives the disk interface with the same number (SetupAPI), the parents
of that disk's device node are walked up to the USB device node, and that node must have the vendor `1209`, the storage PID
`0003` and the board's serial number (its instance ID). The disk's vendor `VHAL` and product `RAM Disk Demo` must match too. A
board with no serial number is refused before anything is written. A volume whose chain cannot be read is never written to,
and the check stops with an error. Then it checks that `README.TXT` contains `VHAL RAM disk demo` and `INFO.TXT` contains
`USB ID: 1209:0003`. It writes a test file of random bytes (`VHALCHK.BIN`, a new
file, never overwriting one), the size of whole sectors of the free space, one sector kept free, at most 1 MiB. It flushes the
file, reads it back with `FILE_FLAG_NO_BUFFERING` into a page-aligned buffer, so the bytes come from the device and not from
the cache, compares the bytes, and deletes the file.

- Changes on the PC and the volume: the test file is written and deleted. The volume is left as it was. The demo's disk is a
  RAM disk, so its contents are restored by a reset anyway. If the write fails part way, the partial file is deleted as well.
- The label only finds the candidates. The device chain above decides which volume is written to, so another stick with the
  same label is not used.

### dfu

First runs `dfu-util -l -d 1209:0004` (the `dfu-util list` step). It takes the `serial="..."` text of the device whose serial equals the
board's, ignoring ASCII case, and passes that text to `-S`. dfu-util compares `-S` case-sensitively, and Windows shows the serial in
lower case, so the listed text is the one that matches. No device with the board's serial, or two different ones, is an error, and no
transfer runs. A board with no serial number is run without `-S`, and the list step is left out.

Then runs dfu-util with `-d 1209:0004 -a 0 -S <listed serial>`: a download of a random file the size of the DFU region (32768 bytes,
`DemoProducts::DfuRegionSize`), then an upload of the region, and a byte comparison.
The file's CRC-32 (zlib) is printed, so it can be compared with the line the board prints. With `--console`, the console is
read at 115200 while dfu-util runs, and the CRC on the `download complete bytes=32768 crc32=0x........` line must equal the
file's.

- Changes on the board: the download writes the DFU region, the top 32 KB of flash (`0x08018000` to `0x0801FFFF`). The region's
  old contents are lost. The demo image is not touched, because the image ends below the region. The upload does not change
  the board.
- Changes on the PC: one file and one folder in the Windows temp folder:
  - the file `usbclasscheck-dfu-<process ID>-<tick count>-<random number>-download.bin`;
  - the folder `usbclasscheck-dfu-<process ID>-<tick count>-<random number>`, which holds `upload.bin`.

  The random number comes from the system's random device. The check creates the download file as a new file, so a file
  that already exists is never overwritten. The folder is created new as well (`CreateDirectoryW` refuses one that exists),
  so no other process is likely to have a file in it. The check does not create the upload file: dfu-util 0.11 opens its
  upload file exclusively and refuses one that exists, so dfu-util creates it. After the run the check deletes the upload
  file if it exists, then removes the empty folder. Only the files and folders this run created are deleted.
- The temp folder is found with the wide API, so a Windows user folder with characters outside ASCII gives a working path.
- dfu-util must be installed, and WinUSB is bound to the board by the demo's MS OS 2.0 descriptors, so no Zadig step is needed.
  The version used so far is 0.11. A board that is not in DFU mode is reported as a device problem (exit 1).

### network

USBNetwork (`1209:0005`) is a [CDC-NCM](../../Class/CDC/NcmFunction.md) Ethernet device, so Windows shows it as a network adapter. The board answers on `192.168.7.1`
and leases `192.168.7.2` to the PC. The steps run in order, and each one prints PASS or FAIL:

- `MAC`: the host's MAC and the board's MAC, derived from the board's serial number with the rule in `DemoProducts.h`
  (`NetworkMacsFromUniqueId`). The serial is the 24 hex digits of the unique ID, so the MACs need no board. A board with no serial
  number cannot be checked (error).
- `adapter`: the adapter whose MAC is the host's MAC (GetAdaptersAddresses). It prints the adapter's name (for example `Ethernet 3`),
  its driver description and the receive and transmit link speeds. No adapter with that MAC is an error, and so is more than one.
- `adapter up`: the adapter is up. The check waits for this and for the lease below. If the adapter cannot be read during the
  wait, the `lease address` step fails with the reason.
- `lease address`: the adapter has `192.168.7.2` and the address is preferred, which means duplicate address detection has
  finished. The check polls every 500 ms for up to 30 s. A lease that does not come in time is a FAIL.
- `lease mask`: the lease's mask is the demo's netmask, `255.255.255.0`.
- `lease gateway`: the lease names no router. The demo's lease has none, so a gateway is a FAIL.
- `ping N bytes`: four echo requests to `192.168.7.1` for each payload size of 0, 32, 1000 and 1472 bytes, sent through
  IcmpSendEcho2Ex with the leased address as the source (the adapter's own address, not the address of another adapter). A reply must come from
  `192.168.7.1`, with status success, and carry the bytes that were sent. The payload is a new pattern for each echo. A size
  passes when all four replies match. The step prints the count and the round trip times (min, average, max). A timeout (2 s
  per echo) or a wrong reply is a FAIL.
- `ARP entry`: the neighbour table has `192.168.7.1` on the adapter's interface, with the board's MAC (GetIpNetTable2).

- Changes on the PC: none to the settings. No adapter setting, route or DHCP setting is changed, and the lease is neither renewed
  nor released. The echo requests and the ARP request fill the ARP cache entry for the board, as any ping does; Windows ages the
  entry out. Nothing is written to disk.
- Changes on the board: none. The board counts the echoes and the ARP replies in its status line (`icmp`, `arp`).

The check needs the lease, so it runs after the board has been plugged in and Windows has taken the lease (a few seconds).

### audio

USBAudio (`1209:0006`) is a [USB Audio 1.0](../../Class/Audio/README.md) device: Windows shows a recording device and a playback device for it, bound by the
in-box driver, with no WinUSB and no INF. The steps run in order. The tone is `DemoProducts::AudioToneFrequency` (1 kHz) and the
loopback test tone is 440 Hz, which is not in `DemoProducts` because only this check needs it.

- `board`: the board must have a serial number. Its USB device node is found through the serial, so without one the check stops
  with an error.
- `microphone` and `speaker`: the board's active capture and render endpoints, found through the MMDevice API. The board's USB device
  node has a container ID (`DEVPKEY_Device_ContainerId`, read with the configuration manager), and an endpoint belongs to the board
  when its own container ID is the same. Windows gives the board's endpoints the board's container ID, and another board's endpoints
  have another one. The name is shown in the report but never used to match. A board whose USB node has no container ID is an error, and
  so is more than one endpoint with the container ID. The step detail gives the endpoint's name, its mix format (the sample
  rate, the channels and the sample size) and the stream's mode, which is one of four: `raw stream: Windows effects bypassed`
  (the raw request was granted), `raw refused: Windows effects may change the sound` (the request was refused), `raw granted, but the stream fell back to the
  Windows mix: Windows effects may change the sound` (the request was granted, but the stream could not be initialised raw, and it
  was opened again on the Windows mix), or `Windows effects may change the sound: the endpoint
  has no raw mode` (the endpoint does not report raw processing). The name is shown in UTF-8, as Windows gives it.
  - Raw streams: the check opens its streams raw where the endpoint allows it. An endpoint reports this with the property
    `PKEY_Devices_AudioDevice_RawProcessingSupported`, and when it is true the stream asks for `AUDCLNT_STREAMOPTIONS_RAW`
    (through `IAudioClient2::SetClientProperties`, before the mix format is read and before the stream is initialised). A raw stream
    skips the endpoint's Windows effects. The reason is measured: on Windows 11, Windows Voice Clarity (noise suppression, the
    `VocaEffectPack` of the board's capture endpoint) suppressed the board's steady 1 kHz tone as noise, while the loopback's tone,
    which starts abruptly, passed. The check changes no Windows setting: the effects are bypassed for the check's own streams only.
    The stream's step names its mode (see above). When the request is granted but the stream cannot be initialised, the check opens the stream
    once more on the Windows mix, without the raw request, and that stream is reported as `raw granted, but the stream fell back to the
    Windows mix`. Only a failure to initialise after a granted request is retried. A failure before it, such as a mix format that
    cannot be decoded, is reported as it is, and a retry that fails too names both errors.
    When a tone step fails on a stream that is not raw (refused, fell back to the Windows mix, or no raw mode), its detail adds `likely cause: Windows effects on the
    endpoint, which a stream that is not raw does not bypass`. The tone and loopback microphone steps use the recording's own mode for
    this cause. When the probe (the stream the `microphone` step opens to read the endpoint's format) ran in another mode, the detail
    also says `; the probe showed <mode>, the recording used <mode>`. The loopback step adds the same cause when it fails on a microphone that is not raw.
  - Windows lists a board's endpoints some seconds after the board enumerates. When none is listed yet, the check waits up to 15 s
    for the endpoint, polling every 500 ms, and prints one line on stderr saying it is waiting. The wait is made for the microphone
    and then for the speaker, and each one ends when the endpoint appears or the 15 s are up. Several endpoints are reported at once.
- `microphone mute`: reads whether the microphone is muted in Windows. A muted microphone records silence, so it is an error and the
  check stops there, as it does for a muted speaker. The check does not unmute it.
- `speaker in use`: before the tone steps, lists the active audio sessions on the board's speaker that belong to other applications
  (this process is left out, and so is the Windows system-sounds session). It only reads the sessions. Each session's process is
  named by its image file name, read from the session's process ID, so the report shows what holds the speaker. One session gives
  `the board's speaker is in use by chrome.exe`; several give the count and every name, for example
  `the board's speaker is in use by 2 sessions: chrome.exe, Discord.exe`. A process whose name Windows does not give (another user's
  process, for one) is named by its process ID. It is an error, not a tone failure: the loopback is then reported as not run. A
  session list that cannot be read is an error too, and the loopback is still tried.
  - Windows makes a newly attached audio device the default output and input device, and the user's applications then play to it.
    So before the audio check, set the default playback and recording devices back to the PC's own devices (Settings, Sound), and
    close the applications that play to the board. The check refuses to run the loopback while other applications play to the board's
    speaker, because their sound would mix into the microphone's recording.
- `tone frequency`: records the microphone for one second with nothing played. The 1 kHz target must be at least 20 dB above the
  strongest of the other frequencies the check measures (220, 440, 500, 750, 1000, 1500, 2000, 3000, 4000 and 6000 Hz, leaving out
  the target and the frequencies within 5 percent of it), and the zero crossings must give 1000 Hz within 2 percent.
- `tone level`: the RMS of the recording must be between -30 and -6 dBFS. The board sends 8000 LSB peak, which is -15.26 dBFS as an
  RMS, and the window allows for the microphone volume set on the host.
- Capture glitches: the microphone flags a packet that follows a gap in its data (a discontinuity). A discontinuity inside a
  measurement window fails that window's steps with `capture glitch: N discontinuities`, whatever the measure found. The step
  names where each one is, in milliseconds from the start of the recording (for example `capture glitch: 1 discontinuity at 512.3 ms`).
  Every step prints the count of discontinuities in its window, so a clean window reads `0 discontinuities`.
  - Start flag: the first packet after the stream starts can carry the flag too. That flag is not a glitch. It is left
    out of the count, and the step says `start flag seen and ignored (the first packet after Start)`. A later flag counts, and
    fails its window as above.
- `speaker mute`: reads whether the speaker is muted in Windows. A muted speaker is an error, because no sound would reach the board.
  The check does not unmute it.
- `tone before playback`: the recording's first part (0.1 s to 0.45 s, before the speaker plays) still has the 1 kHz tone.
- `loopback`: records 1.6 s and plays a 440 Hz tone at a quarter of full scale into the speaker from 0.5 s to 1.5 s. Between 0.8 s and
  1.4 s the microphone must show 440 Hz with the same margin over the other frequencies, including 1 kHz, and a level of at least
  -60 dBFS. A pass means the microphone switched from its own tone to the speaker's audio. A failure also shows the speaker's mute
  and master volume in Windows (read only), so a speaker turned down to zero reads as such and is not taken for a microphone that
  did not hear it. Its detail also gives the speaker's mode (`speaker: ...`) on every run, and the probe difference when the recording ran in another mode.

Changes on the PC: the board's microphone and speaker are opened in shared mode for about 1.6 s and released afterwards. The
speaker plays the 440 Hz tone for one second, and the tone is audible on whatever the board's speaker is. Only the board's two
endpoints are opened. The speaker's active sessions and the mute and master volume of both endpoints are read, never set. The
default devices, the volumes and the mutes are not changed, and no other endpoint is used. The default devices stay where they
were set, so the defaults that Windows gave the board must be changed by hand, as described above.

Changes on the board: none. The board's status line shows the microphone and speaker streams, and `loopback on` while the speaker
plays, so it can be read next to the report.

The board's endpoints are named "... (VHAL audio)" in the sound settings, which the report shows. The streams are raw where the endpoint
allows it (see `microphone` above). Measured on one board with the current build, the audio check passed with raw streams on the microphone and the speaker. Other
Windows 11 PCs are not measured yet. A tone that fails on a raw stream is reported with its measured values, and the detail gives them.
The start flag was on the first packet after Start in both recordings of that run, with no other discontinuity. A discontinuity
that is not the first packet after Start shows its place in the step, and its tone steps fail.

### list

Prints every board whose USB ID is one of the six demo IDs, with its PID, the demo name and its serial number. The serial is
the device's instance ID, which Windows builds from the serial string of the board; a board without one shows
`(no serial number)`.

## Layout

```
CMakeLists.txt             build: the tool (Windows only) and the offline tests
src/Options.*              command line, usage text
src/ExitCodes.h            the exit codes (no Win32)
src/Report.*               steps, verdicts, exit code and the report text
src/DeviceIdentity.*       instance IDs, board selection, serial matching, demo names (no Win32)
src/StatusAnswer.*         the command line's answers: prompt, echo, body, status fields (no Win32)
src/RawInputLogic.*        key sequence and mouse movement checks (no Win32)
src/CheckData.*            pattern, sizes, dfu-util arguments and output, CRC line (no Win32)
src/Crc32.*                the zlib CRC-32 (no Win32)
src/Text.h, WinText.h      report formatting; wide strings and Windows error text
src/DeviceTree.*           SetupAPI interface enumeration, the device tree, Ports and PortName, Raw Input device names
src/SerialPort.*           COM port: line coding, DTR, reads and writes
src/ConsoleCapture.*       the board's console, read on its own thread during a dfu run
src/RawInputListener.*     message-only window and the Raw Input events of one board
src/WinUsbVendor.*         the vendor channel: WinUSB open, endpoints, the loopback
src/TransferLogic.*        the loopback's read accumulation and the time left to a deadline (no Win32)
src/VolumeAccess.*         the volume label, the disk identity, the file calls, the uncached read
src/ProcessPoll.*          the poll loop of a dfu-util run: read the output, wait or time out, with the deadline checks (no Win32)
src/ProcessRun.*           runs dfu-util and collects its output
src/CheckComposite.*       the cdc, hid and vendor checks
src/CheckStorage.*         the storage check
src/CheckDfu.*             the dfu check
src/NetworkLogic.*         network: MAC derivation, adapter matching, lease, echo and ARP results (no Win32)
src/NetworkAdapters.*      network: the adapter table and the neighbour table (GetAdaptersAddresses, GetIpNetTable2)
src/IcmpEcho.*             network: echo requests through a source address (IcmpSendEcho2Ex)
src/CheckNetwork.*         the network check
src/AudioFormat.*          audio: sample layouts, decoding to mono and encoding of the tone (no Win32)
src/AudioLogic.*           audio: Goertzel amplitude, zero crossings, level, the tone generator and the verdicts (no Win32)
src/AudioEndpoints.*       audio: the board's capture and render endpoints (MMDevice, container IDs), mute, the shared stream
src/AudioStreams.*         audio: the record and play loop, timed by the samples recorded
src/WinAudioCom.h          audio: the COM pointer and the COM initialisation
src/CheckAudio.*           the audio check
src/Commands.*             list, board selection, the run
src/main.cpp               entry point
test/*Test.cpp             offline tests of the pure units: options, report, identity, status answers, Raw Input logic, CRC-32, check data, network logic, audio logic, audio format, transfer logic and the dfu poll loop (`test/ProcessPollTest.cpp`)
```

## Limits

- The COM port is found on the CDC control interface (MI_02), which is where the serial driver binds a composite serial port.
  If Windows binds it elsewhere, `cdc` reports that no COM port was found.
- The storage demo's serial is the serial of the USB device node above its disk, which is the instance ID Windows builds from
  the board's serial string. A board whose serial Windows built itself has no serial number, and the storage check refuses to
  write to it. A volume is not written to unless its disk is on this board.
- The mouse and key checks depend on Windows delivering the board's Raw Input events under its HID device. They are not
  measured yet.
- The network check finds the adapter by the MAC derived from the serial number. A driver or a registry override that changes the
  adapter's MAC means the adapter is not found, and the check reports it as not present.
- The audio check matches the endpoints through their container ID. If Windows gives a board's endpoint a container ID other than the
  board's USB device node's, the endpoint is not found and the check reports it as not present. The endpoint's friendly name is not
  checked against anything. An endpoint that is not listed within the 15 s wait is reported as not present, for each of the two.
- Text is UTF-8 on the console, in the file paths and in the Windows messages. The command line is not: `argv` comes in the system code
  page, so a `--dfu-util` path or a `--serial` with characters outside that page is not read as typed.
- The DFU check selects the board by the serial that `dfu-util -l` lists, compared ignoring case. A board that dfu-util does not list
  with its serial is reported as not found.
- The audio check measures what the endpoint delivers, so a Windows audio enhancement (an audio processing object on the endpoint,
  or the microphone's automatic gain) changes the result. The streams are raw where the endpoint allows it, which bypasses the
  endpoint's effects; an endpoint that does not allow it keeps them, and its steps say so.
- The tests do not cover the Win32 code: it compiles, and it is checked on a board.
