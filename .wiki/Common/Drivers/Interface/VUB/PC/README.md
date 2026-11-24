# VUB::PC

The links on a PC: a serial port, a TCP connection, a vendor bulk interface of a USB device, and a HID device. They are in the namespace
`VUB::PC`, in headers of `Common/Drivers/Interface/VUB/PC/`, and each header is header-only. A header includes nothing that a chain does
not use, so a program that does not include a link pays nothing for it.

| Link | Header | Talks to | Kind | Traits |
|---|---|---|---|---|
| [SerialLink](SerialLink) | `PC/SerialLink.h` | a serial port: a UART behind a USB-serial adapter, or a CDC device | stream | full duplex, reports its state |
| [TcpLink](TcpLink) | `PC/TcpLink.h` | a TCP connection, as the client (`Connect`) or the listener (`Listen`) | stream | reliable, full duplex, reports its state |
| [BulkLink](BulkLink) | `PC/BulkLink.h` | a vendor bulk interface of a USB device (VendorBulkFunction) | stream | reliable, full duplex, reports its state |
| [HidLink](HidLink) | `PC/HidLink.h` | a HID device, with the report layout of the device's `HidLink` | packet | framed, full duplex, reports its state |

Each type has a Windows implementation and a Linux implementation, chosen by the compiler (`_WIN32` or `__linux__`). Any other system
stops the build with an `#error`.

## The chain

A PC link is the last block of a chain, as on the device. The stack takes the link object in its constructor and keeps a pointer to it,
so the link object must outlive the stack and must not be moved:

```cpp
VUB::PC::TcpLink link;
NetStack stack(link);
```

The application opens the link, and then calls `Process` in its loop. Each link has the three members the core asks for, and the stack
calls them:

- `Poll(Receiver)` is called by the stack's `Process`, before the received bytes and packets are drained, so what the link reads is handled
  in the same call. It reads at most the receiver's `Room()`, and it never waits.
- `Write(span)` (a stream link) or `Send(span)` (a packet link) is called from `Process` while the link is up. Neither waits: a link that
  cannot take the data now returns zero bytes, or `busy`.
- `SetStateSink` gives the link the sink it reports its state into. The stack reads the state in `Process` (`LinkUp()`).

## Opening, closing and reopening

- `Open`, `Connect`, `Listen` and `Close` are called by the application, not by the stack. An open link reports up. `Close` reports down
  when the link was up.
- An I/O error (an unplugged adapter, a reset device, a peer that closed) reports the link down and closes the device or the socket. The
  link stays closed. The application opens it again to come back: `Open` for a serial port, a bulk interface or a HID device, `Connect` for
  a TCP client.
- A TCP listener is the exception: after its peer goes away it keeps listening, and it accepts the next peer in `Poll` without a new
  `Listen`.
- A link whose `Open` fails stays closed, and `Open` closes a link that is open before it opens it again.

## Closing with transfers in flight

A link on Windows (overlapped I/O) or Linux (libusb transfers) can have a transfer that the OS still writes into. Its buffer is the
link's own, so the link frees nothing until the transfer has completed. The release is shared by the links and kept in
`PC/TransferRelease.h`:

- An I/O path (a read or write that fails in `Poll`, `Write` or `Send`) cancels the transfers and does not wait. Poll finishes the
  release once they have completed.
- `Close` waits up to 200 ms for the transfers in flight, then leaves the rest to `Poll`.
- The destructor waits until every transfer has completed.
- `Open` returns `busy` while an earlier release still waits.

A TCP link and the Linux links of a serial port or hidraw node have no transfer in flight, so their release is immediate. The Windows
serial, HID and bulk links have overlapped reads and writes and follow the rules above.

## Libraries to link

Each test names its libraries per system, and the same names are what an application links.

| System | Libraries | Used by |
|---|---|---|
| Windows | `ws2_32` | TcpLink (Winsock, `getaddrinfo`, `inet_pton`) |
| Windows | `setupapi` | BulkLink, HidLink (device enumeration) |
| Windows | `cfgmgr32` | BulkLink (the serial number of the USB device above the interface) |
| Windows | `winusb` | BulkLink (WinUSB) |
| Windows | `hid` | HidLink (`HidD_*` and `HidP_*`) |
| Windows | none | SerialLink (kernel32 only) |
| Linux | `usb-1.0` | BulkLink (libusb) |
| Linux | none | SerialLink (termios), TcpLink (POSIX sockets), HidLink (hidraw) |

The headers of the Linux links need `libusb-1.0` headers for BulkLink (`libusb-1.0/libusb.h`) and the kernel's `linux/hidraw.h` for
HidLink.

## Windows headers

`windows.h` is included with `WIN32_LEAN_AND_MEAN` defined, unless the program has defined it. A program that includes `<windows.h>`
itself, without that macro, before a PC header gets the full Windows header, and `winsock2.h` cannot be included after it. The
`hidsdi.h` and `hidpi.h` headers are included inside `extern "C"`, which the MinGW headers need for the `HidD_*` and `HidP_*` calls to link.

## Tests

The tests are in `Common/Drivers/Interface/VUB/.tool/sim/`. They never open a device, a serial port or a USB interface. The TCP tests bind
`127.0.0.1` and let the system pick the port. The serial test on Linux uses a pseudo-terminal pair (`openpty`). The bulk and HID tests are
compiled on both systems and check their pure parts (the serial match, the GUID text, the report layout, the usage page) as plain
functions. `TestVubPcRelease.cpp` checks the release (`TransferRelease.h`) with a fake handle layer: the waits, the bound of 200 ms, and that
the handles are never freed while a transfer is in flight. `TestVubPcHidRelease.cpp` (Windows) checks that a `Send` while a report is on its
way is busy and leaves the report's buffer alone, and that `Close` returns within the bound with the handles kept. The snippets of these pages
are compiled by `TestVubPcWiki.cpp`.
