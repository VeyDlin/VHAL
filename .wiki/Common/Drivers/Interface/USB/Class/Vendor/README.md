# Vendor Bulk Function

A vendor-class function with one bulk IN and one bulk OUT endpoint. It is the data path for raw data between the MCU and a PC. It should need no class driver on hosts with a generic USB API (libusb, IOKit, the Android USB host API). It is verified on Windows 11 and on Linux (see [Measured Results](../../README.md#measured-results)); macOS and Android are untested. On Windows it needs the optional [MS OS 2.0 component](../../Descriptor/MsOs20/README.md) to bind WinUSB.

Headers:

- `#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>`: buffered mode
- `#include <Drivers/Interface/USB/Class/Vendor/VendorBulkStreamFunction.h>`: streaming mode

Both modes derive from `VendorBulkFunctionBase`. The base owns the endpoints, the transfer state and the rules the two modes share.

## Descriptors

The application writes the descriptors. For this function, it needs:

- One interface with `bInterfaceClass` 0xFF (vendor). `bNumEndpoints` is 2. The subclass and protocol are the application's.
- One bulk IN and one bulk OUT endpoint. `wMaxPacketSize` is 64, and `bInterval` is 0. The addresses can be given in either order. Use `0x81` and `0x02`: that pair gets double buffering on a port that supports it. `0x81` and `0x01` configure too, but on fsdev they share one endpoint register and stay single-buffered.

The function checks these when the host configures the device. `OnConfigured()` returns `invalidParameter` for any of the following:

- not exactly one interface
- not exactly one IN and one OUT endpoint
- an endpoint address with a reserved bit set, or endpoint number 0

Each endpoint opens with one buffer first, then is reopened with double buffering. Both single-buffered opens must succeed: any failure fails `OnConfigured()` and closes what was opened. Only the upgrade to double buffering accepts `notSupported`, which keeps the single buffer, so configuration succeeds on a port that cannot give double buffering.

The fluent builder writes these descriptors with `USBVendorBuilder` (see [Descriptor](../../Descriptor/README.md#class-builders)). The function is then built from the layout of the same chain, so its interface and endpoints cannot differ from the descriptors:

```cpp
using AppUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		// ... identities, MsOs20VendorCode() ...
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().Name("Vendor bulk").BulkIn(0x81).BulkOut(0x02)));
}>;

VendorBulkFunction<4096, 4096> vendor(AppUsb::Function(0));
```

`AppUsb::Function(0)` is a `USBFunctionLayout`: the interface numbers, the endpoint addresses in the order the vendor builder gives them (IN, then OUT), and the function's slice of the configuration. The function keeps the number and address spans. The `static` storage of the chain keeps them valid.

The span constructor, `VendorBulkFunction(interfaces, endpoints)`, is the form for hand-written descriptors. It takes the same two arrays, in the same order.

## Packets and Transfers

- A packet is 64 bytes: `VendorBulkFunctionBase::PacketSize`.
- An IN transfer is `PacketsPerTransfer` packets. The default is 8, which is 512 bytes. The template takes the value.
- An OUT transfer is one packet, 64 bytes, whatever `PacketsPerTransfer` is. Each packet the host sends completes its own transfer, and the endpoint is armed again for the next one. A short packet completes a transfer as well, so a host write of any length reaches `Read()` whole, with or without a zero-length packet after it. See the rule below.
- One OUT transfer is in flight at a time. Double buffering in the port keeps the bus busy while one buffer is handled.
- IN uses two stages (`VendorBulkFunctionBase::InStageCount`, which is 2). One stage is in flight while the thread (`Process()`) fills the other, so the interrupt that completes a transfer only starts a stage that is already filled. The stage size is the template's `PacketsPerTransfer` * 64 bytes, so the RAM for IN is twice that: `2 * PacketsPerTransfer * 64` bytes. OUT keeps one 64-byte buffer.
- How an IN run ends. A count that is not a multiple of 64 ends in a short packet. A whole number of packets ends in a zero-length packet only when nothing waits behind it: the stage behind it is not filled, and the TX ring holds no bytes past the stage. The rule is the same for a stage of any size, a short one or a full one. Then the host's read, which can be larger than one buffer, ends there. When data follows at once, no packet is added and the next buffer starts at the completion. A stage's zero-length packet is decided when the stage starts, so a stage that starts with nothing behind it keeps its zero-length packet, even if more bytes are written before it completes. A zero-length packet sent when it was not needed costs one packet on the bus. A missing one leaves the host waiting.
- An OUT endpoint with no room for one packet NAKs. The host keeps its data until the application frees room.
- Why OUT is one packet: an OUT transfer completes only on a short packet or when its buffer is full. A host write that is a whole number of packets, with no zero-length packet after it, therefore stayed in a multi-packet OUT buffer and never reached `Read()`. On the first hardware run (Windows 11, `VendorBulkFunction<512, 512, 4>`) a 64-byte write hung, and the device's looped counter stayed at 64. Windows (usbser.sys and WinUSB) sends no zero-length packet by default, so a one-packet buffer is the one that delivers every write whole. The CDC-ACM data interface uses the same channel and the same rule.

## Buffered Mode

`VendorBulkFunction<TxBufferSize, RxBufferSize, PacketsPerTransfer = 8>` keeps two ISR-safe rings. The application writes into the TX ring and reads from the RX ring. The function moves the bytes between the rings and the bus.

```cpp
VendorBulkFunction<4096, 4096> vendor(AppUsb::Function(0));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `Write(data)` | `uint16` | Queues bytes for the host. Returns how many were queued. Less than `data.size()` when the TX ring is full, and 0 when not configured |
| `Read(data)` | `uint16` | Copies received bytes out. Returns how many. 0 when not configured |
| `GetReadableCount()` | `uint16` | Received bytes waiting for `Read()`. 0 when not configured |
| `GetWritableCount()` | `uint16` | Bytes `Write()` can queue now. 0 when not configured |

Rules:

- `Write()` does not start a transfer. `Process()` fills the free IN stage and starts a transfer when the IN endpoint is idle. Each IN completion starts the next stage that `Process()` has already filled. Call `device.Process()` from the main loop, so that a short write is sent, and so that the next stage is ready before the host's next token.
- When no stage is filled at a completion, the interrupt produces the next stage itself. A function that calls `Process()` only once still keeps sending, but it does the fill work in the interrupt.
- `Read()` frees room in the RX ring. `Process()` arms the OUT endpoint again once one packet fits.
- `RxBufferSize` must hold one OUT packet (64 bytes). The template checks this with `static_assert`, and `PacketsPerTransfer` must be at least 1.
- The rings are cleared on bus reset and on configuration.

```cpp
// Thread context. A count below bytes.size() means the TX ring was full. Retry with the rest later.
uint16 SendBytes(std::span<const uint8> bytes) {
	return vendor.Write(bytes);
}


// Thread context, from the main loop.
void ReceiveLoop() {
	uint8 buffer[64] = {};
	uint16 count = vendor.Read(buffer);
	for (uint16 index = 0; index < count; index++) {
		// Handle buffer[index].
	}
}
```

## Streaming Mode

`VendorBulkStreamFunction<PacketsPerTransfer = 8>` is the fast path. It has no rings. The application supplies two callables:

- a producer, which fills the IN transfer buffer
- a consumer, which takes the bytes of each received OUT packet

The consumer runs in interrupt context, and the endpoint is re-armed at once. The consumer must take every packet: there is no backpressure, so a packet it does not keep is lost. Each OUT transfer is one packet, so the consumer is called once per packet, with 1 to 64 bytes. A host write of N packets gives N calls, and a write of 576 bytes gives nine calls of 64 bytes. The span is valid only during the call. The producer runs in `Process()` when an IN stage is free, so it is not called at the end of every transfer. It runs in interrupt context only when no stage is filled, which happens when `Process()` is not called after a completion.

A stream never ends a run of whole packets with a zero-length packet. Its host reads the length it asked for (the bench protocol), and a zero-length packet would end that read early, which usbbench counts as a failure. So a whole number of packets just completes its stage, and the next stage goes out when the producer has bytes for it. A count that is not a multiple of 64 still ends in a short packet, and only the last packet of a stream may be short. A producer whose protocol needs a terminator sends a short packet itself, as its last bytes. The stream answers `EndsIdleRunsWithZeroLengthPacket()` with `false`, so the engine never adds one.

```cpp
// Thread context from Process(), or interrupt context when no IN stage is filled. Fills the packets with a counter and returns how many bytes it wrote.
size_t NextCounterBytes(std::span<uint8> packets) {
	static uint8 counter = 0;

	for (uint8& value : packets) {
		value = counter;
		counter++;
	}

	return packets.size();
}


VendorBulkStreamFunction<8> stream(AppUsb::Function(0));

// Before device.Start(). Refused with busy once the function is configured.
stream.SetProducer(NextCounterBytes);
stream.SetConsumer([](std::span<const uint8>) {
	// Interrupt context. Called once per OUT packet, 1 to 64 bytes. The span is valid only during this call.
});
```

| Method | Returns | Description |
|--------|---------|-------------|
| `SetProducer(Producer)` | `ResultStatus` | Sets the producer. `busy` while configured |
| `SetConsumer(Consumer)` | `ResultStatus` | Sets the consumer. `busy` while configured |

Rules:

- The producer returns a count of at most `packets.size()`. Without a producer, the IN endpoint sends nothing.
- Without a consumer, the OUT endpoint is not armed, so the host's data waits.
- A producer that needs an unbroken sequence must resynchronise after a halt. The function does not replay bytes. A halt or a failed start loses the bytes the producer already handed out for every IN stage, the one in flight and any filled behind it.
- The callables are `Callback::Delegate`s: a function pointer and a context pointer, so they never allocate. A stateful producer or consumer passes its object as the context to `Delegate::FromRaw()`, and the object must outlive the function. A lambda that captures is not used. Setting one happens before `Start()`, not on the data path.

## Interrupt Context and Critical Sections

The rules are the same for both modes.

| Code | Context | Rule |
|------|---------|------|
| `OnTransferComplete`, `OnEndpointHaltChanged`, `OnBusReset` | Interrupt | Short. No blocking, no allocation |
| Consumer | Interrupt | Short. Keep it simple and bounded |
| Producer | Thread (`Process()`), or interrupt when no IN stage is filled | Short. Keep it simple and bounded |
| `Write()`, `Read()`, `GetReadableCount()`, `GetWritableCount()` | Thread | Safe with the interrupt. The rings lock themselves. They may also be called inside your own critical section |
| `SetProducer()`, `SetConsumer()` | Thread | Before `Start()` |
| `device.Process()` | Thread | Starts IN transfers and arms OUT transfers |

The critical-section rule: `System::CriticalSection` nests on every port. Only the outermost leave restores the interrupt state that the outermost enter found, so a ring call inside your own section is safe, and so is a call to `GetTick()`. Keep your section short: it masks the interrupts for its whole length, and a long fill or a long copy delays every interrupt.

The function's own `Process()` uses short sections of its own. It claims the IN side when a stage is free, fills that stage outside any section, and ends the claim in a short section that also starts the head stage. The fill stays outside a section so that the sections stay short, because a stage copy takes time. While it holds the claim, the interrupt does not touch the IN queue. A completion or a halt only sets a pending flag, and `Process()` applies it before it ends the claim: the completed stage is committed, or every stage is rolled back.

One exception: a bus reset or a configuration clears the TX and RX rings (`ResetData()`) from the interrupt, even while the claim is held. Each ring call locks itself. The application calls `Write()` from the thread that runs `Process()`, so nothing is added to the TX ring between the clear and `Process()`'s marks. A mark made before the clear is rolled back when the claim ends. A mark made after it fails, because `RingBuffer::MarkRead()` checks the size. If the session ends during that commit, the queue is not advanced, because the reset has already cleared it.

## Halts and Resets

- A halt on the IN endpoint abandons the transfer in flight, and the stage filled behind it is dropped too. Their bytes go back to the TX ring (buffered) or are lost (streaming, see above). The function starts again after the clear. A transfer that completed before the halt is committed first, so only unsent bytes come back.
- Buffered IN commits by count. When a transfer completes, exactly its bytes leave the TX ring, and the stage behind it stays outstanding. A rollback returns every outstanding byte, in order.
- A halt on the OUT endpoint drops the packets stored in the current transfer. Nothing has reached the RX ring yet.
- A bus reset clears the rings and drops the transfers. A configuration does the same. A session counter changes at each of these. `Read()` and `Process()` compare it, so bytes from an old session are not returned.

## Benchmark

The benchmark firmware (`.demo/STM32G4`) uses this function. Its protocol, endpoints and vendor requests are in `.tool/common/BenchProtocol.h`. See the [Tests and tools](../../README.md#tests-and-tools) section.

## Limits

| Item | Value |
|------|-------|
| Packet size | 64 bytes, both directions |
| IN transfer size | `PacketsPerTransfer` * 64 bytes. Default 512 |
| OUT transfer size | 64 bytes, one packet |
| IN stages | 2. IN RAM is `2 * PacketsPerTransfer * 64` bytes, OUT RAM is 64 bytes |
| TX and RX ring sizes | Template parameters. RX holds at least one OUT packet (64 bytes) |
| Interfaces | 1 |
| Endpoints | 1 IN and 1 OUT |
| Full-speed throughput | Limited by the bus: 19 packets of 64 bytes per 1 ms frame, about 1.2 MB/s in each direction |
