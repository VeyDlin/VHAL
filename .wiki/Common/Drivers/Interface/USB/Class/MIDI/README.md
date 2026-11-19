# MIDI Class

A USB-MIDI 1.0 function: an Audio Control interface and a MIDIStreaming interface with one bulk OUT and one bulk IN endpoint, carrying 4-byte event packets. Each cable has an embedded and an external IN jack and an embedded and an external OUT jack. The descriptors are written to the USB-MIDI 1.0 and Audio 1.0 layouts, so Windows, Linux, macOS, iOS and Android are expected to bind the device to their in-box class drivers without a driver of its own. This is not yet tested on hardware: the stack is tested in the host simulator only, and no hardware measurement is recorded for MIDI (see [Host Support](../../README.md#host-support)).

Headers:

- `#include <Drivers/Interface/USB/Class/MIDI/USBMidiBuilder.h>`: the interface, for the configuration builder
- `#include <Drivers/Interface/USB/Class/MIDI/MidiFunction.h>`: the function, the event queues and the event helpers

## Class Builder

`USBMidiBuilder` plugs into `USBConfigurationBuilder::Function()` (see [Descriptor](../../Descriptor/README.md)). It writes the two interfaces, the interface association over both, the class-specific descriptors and the two endpoints. The layout it gives the function lists the Audio Control interface first, then the MIDIStreaming interface, and the bulk OUT endpoint first, then the bulk IN endpoint.

```cpp
using AppUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		// ... identities ...
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMidiBuilder()
				.Cables(2)
				.BulkOut(0x01)
				.BulkIn(0x81)
				.Name("Port")
				.JackName("Jack")));
}>;

MidiFunction<2, 32, 32> midi(AppUsb::Function(0));
```

| Method | Description |
|--------|-------------|
| `Cables(n)` | The number of cables, 1 to 16. Required. Any other value is a compile error |
| `BulkOut(address)` | Address of the bulk OUT endpoint, the host to device direction. Without it the configuration builder assigns the number |
| `BulkIn(address)` | Address of the bulk IN endpoint, the device to host direction. Without it the configuration builder assigns the number |
| `Name(text)` | The MIDIStreaming interface string. Optional |
| `JackName(text)` | The iJack string of every jack. Optional. All jacks of all cables share it |

Without addresses the builder numbers the endpoints as the configuration does: the OUT endpoint takes 0x01 and the IN endpoint the next number free in either direction, 0x82.

The interface association has class 0x01 (Audio), subclass 0x00 and protocol 0x00. The device class is 0xEF/0x02/0x01 because of the association, as for every composite configuration.

## Descriptors

For one cable, the configuration holds these pieces in this order (MIDI 1.0 chapter 6, Audio 1.0 section 4.3):

| Piece | Bytes | Content |
|-------|-------|---------|
| Interface association | 8 | Interfaces 0 and 1, class 0x01 |
| Audio Control interface 0 | 9 | Class 0x01, subclass 0x01, protocol 0, no endpoints |
| Audio Control header | 9 | bcdADC 0x0100, wTotalLength 9, one streaming interface, number 1 |
| MIDIStreaming interface 1 | 9 | Class 0x01, subclass 0x03, protocol 0, two endpoints |
| MS header | 7 | bcdMSC 0x0100, wTotalLength over the class-specific part: 7 + 30 per cable |
| Jacks of each cable | 30 | Embedded IN (6), external IN (6), embedded OUT (9), external OUT (9) |
| Bulk OUT endpoint | 9 | The audio-form endpoint: 64 bytes, bRefresh 0, bSynchAddress 0 |
| MS bulk data endpoint | 4 + n | The embedded IN jacks of the OUT endpoint |
| Bulk IN endpoint | 9 | 64 bytes, the audio-form endpoint |
| MS bulk data endpoint | 4 + n | The embedded OUT jacks of the IN endpoint |

Jack IDs are numbered in blocks of four per cable, from 1: cable 0 has embedded IN 1, external IN 2, embedded OUT 3 and external OUT 4; cable 1 has 5 to 8, and so on. The embedded IN jack takes the packets the host sends; the external OUT jack's source is that embedded IN jack. The external IN jack feeds the embedded OUT jack, which the bulk IN endpoint carries to the host. An OUT jack's source pin is 1, the only output pin of an IN jack.

The MS header's wTotalLength covers the MS header and the jacks, and not the endpoint descriptors that follow the jacks.

The audio-form endpoint is the standard 7-byte endpoint with bRefresh and bSynchAddress added (9 bytes in all). The audio class defines it for bulk and isochronous endpoints, and the stack has no isochronous endpoints, so the configuration accepts it only for bulk endpoints with both added fields zero. Standard 7-byte endpoints keep their rules and may share a configuration with the audio form, as the keyboard and MIDI test does.

## Function

`MidiFunction<Cables, TxQueueEvents, RxQueueEvents>` takes the layout of the function. Cables is 1 to 16 and matches the builder's cable count; the queues hold event packets, and `RxQueueEvents` holds at least one whole OUT transfer (16 packets).

| Method | Context | Description |
|--------|---------|-------------|
| `SendEvent(packet)` | Thread | Queues one complete event packet. `invalidParameter` for a cable outside the function or a malformed packet, `notReady` while unconfigured, `busy` when the queue is full |
| `NoteOn`, `NoteOff`, `PolyKeyPressure`, `ControlChange`, `ProgramChange`, `ChannelPressure`, `PitchBend` | Thread | Channel messages on a cable. Data bytes are 7-bit, the channel 0 to 15; pitch bend is 14-bit, 0 to 16383 |
| `SendSysEx(cable, bytes)` | Thread | A SysEx message as 3-byte start packets and a closing packet. The bytes after the first must be 7-bit, and only a final F7 has bit 7 set (`invalidParameter` otherwise). Queued whole or not at all (`busy` when the queue has no room) |
| `ReadEvent(packet)` | Thread | Takes the oldest received event. `notReady` when none |
| `GetSendableCount()`, `GetReadableCount()` | Thread | Queued events waiting to be sent and received events waiting to be read. Both are 0 while unconfigured |
| `SetEventHandler(handler)`, `SetSysExHandler(handler)` | Thread | Called in interrupt context for each received event, and with the bytes of each received SysEx packet. Return `busy` while configured |

Received packets go to the RX queue and to the event handler, except packets on a cable the function does not have, which are dropped. A SysEx packet also gives its bytes to the SysEx handler as a byte stream, so a message can be reassembled without the queue. The OUT endpoint is armed only while the RX queue has room for a whole transfer, so the host keeps its packets when the thread is behind. Each IN transfer carries up to 16 whole packets; a bus reset or a new configuration clears both queues.

The function's critical sections nest, so the thread calls may use the ring buffers inside their own section. Each section stays short.

## Compile Errors

A rule violation is a compile error with its message:

- `a USB-MIDI function has 1 to 16 cables`: `Cables(0)` and `Cables(17)`
- `a USB-MIDI interface needs its cables: call Cables()`: a function without `Cables()`
- `BulkOut needs an OUT address (bit 7 clear)` and `BulkIn needs an IN address (bit 7 set)`, and `endpoint 0 belongs to the control pipe`
- `bRefresh and bSynchAddress are 0 for bulk endpoints`: a 9-byte endpoint with either field set (see `.tool/sim/BuilderErrors/audio_endpoint_refresh_not_zero.cpp`)
- `the 9-byte endpoint form is bulk`: a 9-byte endpoint that is interrupt or isochronous, from the builder or from a plain array (see `.tool/sim/BuilderErrors/audio_endpoint_interrupt.cpp` and `audio_endpoint_isochronous.cpp`)
- `endpoint address bits 6..4 are reserved`: an endpoint address with a reserved bit set, from a plain array too (see `.tool/sim/BuilderErrors/endpoint_reserved_bits_plain.cpp`)

The snippets are in `.tool/sim/BuilderErrors/midi_*.cpp`, checked by `check.sh`.

## Tests

The host tests are in `.tool/sim` and run with `run.sh`:

- `TestMidiDescriptors.cpp`: the configuration for one and two cables, byte for byte, against arrays written out by hand from MIDI 1.0 and Audio 1.0. Also the association and device class, the automatic and explicit endpoint addresses, the interface and jack names through the string table, and a keyboard and a MIDI function in one configuration.
- `TestAudioEndpointDescriptors.cpp`: the 7-byte and the 9-byte endpoint forms, alone and in one configuration.
- `TestMidiBuilder.cpp`: `MidiFunction` built from the builder's layout, events from the host to the device and back through the simulated host, and a SysEx message.
- `TestMidi.cpp`: the function's queues, SysEx, back-pressure, resets and halts, with a hand-written descriptor set.
