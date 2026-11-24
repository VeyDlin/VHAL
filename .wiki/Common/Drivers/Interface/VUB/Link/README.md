# VUB links on a microcontroller

A link is the block at the bottom of a chain: the wire the stack writes to and reads from. The links of this folder are for a
microcontroller. Each one is a header-only template over the type of the port or the function it uses, so any port or function with
the same members works, and each is its own header: a chain that does not include a link pays nothing for it.

| Link | Header | Over | Kind | Reliable | Reports its state | Receives |
|---|---|---|---|---|---|---|
| [VUB::UartLink](UartLink) | `Link/UartLink.h` | a UART port (`UARTAdapter`, `UARTAdapterG4` on the G431, `UARTAdapterESP` on the ESP32) | stream, not framed | no | no | in the receive interrupt |
| [VUB::CdcLink](CdcLink) | `Link/CdcLink.h` | `CdcAcmFunction` (a USB serial port) | stream | yes | yes: configured and DTR | `Poll` reads the host's bytes |
| [VUB::BulkLink](BulkLink) | `Link/BulkLink.h` | `VendorBulkFunction`, buffered (a USB vendor bulk pair) | stream | yes | yes: configured | `Poll` reads the host's bytes |
| [VUB::HidLink](HidLink) | `Link/HidLink.h` | `HidFunction` (a USB HID interface) | packet, framed by its report | no | no | the output delegate, in the interrupt |

The report layout of the HID link is in [HidReport](HidReport).

## Which one for which device

- A UART port carries a byte stream with no state and no delivery guarantee of its own. Use `UartLink` and put a framing above it
  (COBS, length prefix or gap framing), and a transform or [VUB::Reliable](../Layer/Reliable) above that when the bytes must arrive.
- A USB serial port (CDC-ACM) or a vendor bulk pair is a byte stream that the USB stack keeps in order. Use `CdcLink` or `BulkLink`
  with a framing above it. The USB transfer does not lose bytes, so a reliable layer is needed only for an end-to-end guarantee.
- A USB HID interface carries one report per packet, with a length byte in the report. Use `HidLink` with no framing: the report is
  the frame. A HID report can be dropped by the host, so use [VUB::Reliable](../Layer/Reliable) above the link when a packet must arrive.

## How the receive path works

A link hands received data to its stack through a `VUB::Receiver`, which the stack gives it or the link gets as the argument of `Poll`
(see [receiving from a link](../README#receiving-from-a-link)). The three ways:

- `UartLink` receives in an interrupt. The stack gives it a receiver with `SetReceiver`. From then on the port's receive interrupt
  calls the link once per byte, and the link feeds each byte to the receiver. On the G4 port the interrupt is the USART's; on the ESP32
  port the byte is read in the `uart_rx` task.
- `CdcLink` and `BulkLink` are polled. Their `Poll` runs in `Process`, before the stack takes the packets it has. It reads from the
  function up to the receiver's `Room()`, in pieces of 64 bytes, and feeds each piece. The bytes that do not fit stay in the function's
  ring, so nothing is dropped by the link.
- `HidLink` receives in the function's output delegate. The function calls it in its interrupt for each output report, and the link
  feeds the report's payload to the receiver as one packet. A report whose length byte is 0 or larger than the report is dropped.

The stack gives each receiver in its constructor and takes it back in its destructor with an empty receiver. An empty receiver stops
what the link started: a `UartLink` stops its continuous receive and clears the port's callbacks (once no transfer runs), and a `HidLink` clears the output
delegate. A link never feeds a stack that is gone.

## How the send path works

`Write` (a stream link) and `Send` (a packet link) are called from `Process`, and they never block.

- `UartLink::Write` copies up to `TxBytes` into its own buffer and starts one transfer. It returns 0 while the transfer runs, and the
  stack keeps the rest of the frame for the next `Process`.
- `CdcLink::Write` and `BulkLink::Write` are the function's `Write`: they queue what the ring takes, which can be less than asked, and
  they return 0 while the ring is full or the function is not configured.
- `HidLink::Send` packs the packet into a report and calls `SendReport`. A function that is not configured yet, or whose queue of
  reports is full, returns busy, and the stack keeps the packet.

A link never drops a packet on a busy answer. The stack keeps it and sends it again on the next `Process`.

## Sizes

Each link is a small object: the port or function is held by pointer, and a link keeps only its buffers, its receiver and its state. The
sizes on the host, with the doubles and the real USB classes, are in each link's page. The links carry no code that a chain does not
call, and each header compiles alone for a Cortex-M4 and a Cortex-M0+.
