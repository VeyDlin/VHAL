# ReliableProtocolCOBS

`ReliableProtocolCOBS` sends typed reads and writes over a byte stream. It frames packets with COBS and CRC-16/XMODEM, and waits for a matching ACK or NACK. It is declared in `Drivers/Interface/User/ProtocolCOBS/ReliableProtocolCOBS.h`.

## Wire and callback setup

Provide `rawWrite` to send the complete encoded frame, and install `onWrite` and `onRead` handlers for requests received by this endpoint:

```cpp
ReliableProtocolCOBS<> protocol;

protocol.rawWrite = [&serial](const uint8* encodedBuffer, size_t length) -> ResultStatus {
    return serial.WriteByteArray(
        const_cast<uint8*>(encodedBuffer),
        static_cast<uint32>(length)
    );
};

protocol.onWrite = [](uint16 address, const uint8* data, size_t length) -> ResultStatus {
    if (address != 0x12 || length != sizeof(uint16)) {
        return ResultStatus::invalidParameter;
    }

    uint16 receivedValue = 0;
    memcpy(&receivedValue, data, sizeof(receivedValue));
    return ResultStatus::ok;
};

protocol.onRead = [](uint16 address, uint8* outData, size_t& outDataLength) -> ResultStatus {
    if (address != 0x12) {
        return ResultStatus::invalidParameter;
    }

    const uint16 responseValue = 42;
    memcpy(outData, &responseValue, sizeof(responseValue));
    outDataLength = sizeof(responseValue);
    return ResultStatus::ok;
};
```

Call `RxEvent(byte)` for each received byte. Run `Execute()` repeatedly on a receive task so it can process queued packets and ACKs while a `Read()` or `Write()` waits. The callbacks run on this receive task when it processes a request. A callback returning `ResultStatus::ok` produces an ACK; any other status produces a NACK. Missing read/write callbacks also result in a NACK. `Read()` and `Write()` share transaction state; do not call them concurrently on one instance.

`Write(address, value, timeout, retry)` returns `ResultStatus`. `Read<DataType>(address, timeout, retry)` returns `Result<DataType>`; check `IsOk()` before reading `Value()`:

```cpp
uint16 valueToSend = 7;
ResultStatus writeStatus = protocol.Write(0x12, valueToSend);

Result<uint16> readResult = protocol.Read<uint16>(0x12);
if (readResult.IsOk()) {
    uint16 receivedValue = readResult.Value();
}
```

The default timeout is 1000 ms and the default retry count is 3 total attempts. `MaxPacketSize` defaults to 256 bytes and sizes the receiver's decoded-packet buffer, which includes 11 bytes of packet metadata and CRC. Keep payloads at or below `MaxPacketSize - 11` so a peer with the same setting can decode them. `ReliableProtocolCOBS<MaxPacketSize, AckMailCount>` changes the receive-buffer size and ACK mailbox capacity.

This is a decoded-size ceiling, not a guaranteed payload capacity: escaping and framing also consume space in the fixed encoding buffers. Check the returned status even for smaller payloads.

## Multi-drop addressing

Address filtering is disabled by default, preserving the existing full 16-bit callback address. Call `SetDeviceAddress(uint8 address)` to enable filtering. With filtering enabled, the high address byte selects the device and the low byte is passed to `onRead` or `onWrite`. Packets for a different device are silently ignored so other devices on a shared bus do not NACK them. `DisableDeviceAddress()` restores the default full-address behavior.

With address filtering enabled, the reserved device address `ReliableProtocolCOBS<>::broadcastAddress` (`0xFF`) accepts writes on every addressed device. Each device invokes `onWrite`, when installed, with the low byte as its local address and sends no ACK or NACK. Broadcast reads are ignored. Since `Write()` waits for an ACK, a broadcast write cannot report successful delivery: it times out after each attempt. Retries can repeat the write callback and its side effects, so broadcast handlers should account for duplicate delivery.
