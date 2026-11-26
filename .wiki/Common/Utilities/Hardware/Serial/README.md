# Serial -- UART Receive Buffers

Interrupt-driven UART receive buffering for VHAL. `SerialBuffer` captures incoming bytes via the UART Rx interrupt and stores them in a ring buffer for deferred processing.

---

## Table of Contents

- [Quick Start](#quick-start)
- [SerialBuffer](#serialbuffer)
- [API Reference](#api-reference)

---

## Quick Start

```cpp
#include <Utilities/Hardware/Serial/SerialBuffer.h>

// Declare a 128-byte receive buffer attached to a UART adapter
SerialBuffer<128> rxBuffer(uart1);

// Later, in your main loop:
while (rxBuffer.IsReady()) {
    auto result = rxBuffer.Pop();
    if (result.IsOk()) {
        uint8 byte = result.Value();
        // process byte
    }
}
```

---

## SerialBuffer

**Header:** `SerialBuffer.h`
**Depends on:** `RingBuffer<uint8, BufferSize>` (`Utilities/Buffer/RingBuffer.h`)

A template class that wraps a `RingBuffer` and hooks into an `AUART` (platform UART adapter) Rx interrupt to collect incoming bytes automatically. The bytes are kept in the object (`Memory::Inside`, the default storage of `RingBuffer`).

### Template Parameters

| Parameter    | Type     | Description                         |
|--------------|----------|-------------------------------------|
| `BufferSize` | `uint16` | Maximum number of bytes in the ring buffer |

### Constructor

```cpp
SerialBuffer<256> rxBuf(uart1);
```

Accepts an `AUART&`. Registers an interrupt callback that pushes every received byte into the internal ring buffer.

### Usage Example

```cpp
SerialBuffer<64> rxBuf(uart2);

// Poll for received data
void processSerial() {
    while (rxBuf.IsReady()) {
        auto result = rxBuf.Pop();
        if (result.IsOk()) {
            handleByte(result.Value());
        }
    }
}

// Discard all buffered data
rxBuf.Clear();

// Check how many bytes are buffered
uint16 pending = rxBuf.Size();
```

---

## API Reference

### SerialBuffer\<BufferSize\>

| Method      | Return Type        | Description                                      |
|-------------|--------------------|--------------------------------------------------|
| `Clear()`   | `ResultStatus`     | Clears all data from the buffer                  |
| `IsReady()` | `bool`             | Returns `true` if there is data available to read |
| `Size()`    | `uint16`           | Returns the number of bytes currently buffered   |
| `Pop()`     | `Result<uint8>`    | Removes and returns the oldest byte (`Value()`) with a status (`IsOk()`, `Error()`) |
