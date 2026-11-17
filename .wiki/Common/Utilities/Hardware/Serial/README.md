# Serial -- UART Receive Buffers

Interrupt-driven UART receive buffering utilities for VHAL. Two template classes that automatically capture incoming bytes via UART Rx interrupts and store them in a buffer for deferred processing.

---

## Table of Contents

- [Quick Start](#quick-start)
- [SerialBuffer](#serialbuffer)
- [SerialFIFO](#serialfifo)
- [API Reference](#api-reference)
- [Choosing Between SerialBuffer and SerialFIFO](#choosing-between-serialbuffer-and-serialfifo)

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

A template class that wraps a `RingBuffer` and hooks into an `AUART` (platform UART adapter) Rx interrupt to collect incoming bytes automatically.

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

## SerialFIFO

**Header:** `SerialFIFO.h`
**Depends on:** `StaticFifo<ElementsCount, uint8>` (`Utilities/Buffer/StaticFifo.h`)

A template class that wraps a `StaticFifo` of `uint8` elements and hooks into an `AUART` (platform UART adapter) Rx interrupt to collect incoming bytes automatically.

### Template Parameters

| Parameter       | Type     | Description                              |
|-----------------|----------|------------------------------------------|
| `ElementsCount` | `size_t` | Maximum number of elements in the FIFO   |

### Constructor

```cpp
SerialFIFO<256> rxFifo(uart1);
```

Accepts an `AUART&`. Registers an interrupt callback that pushes every received byte into the internal FIFO.

### Usage Example

```cpp
SerialFIFO<128> rxFifo(uart3);

// Poll for received data
void processSerial() {
    while (rxFifo.IsReady()) {
        auto result = rxFifo.Pop();
        if (result.IsOk()) {
            handleByte(result.Value());
        }
    }
}

// Discard all buffered data
rxFifo.Clear();

// Check how many bytes are buffered
size_t pending = rxFifo.Count();
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

### SerialFIFO\<ElementsCount\>

| Method      | Return Type        | Description                                      |
|-------------|--------------------|--------------------------------------------------|
| `Clear()`   | `ResultStatus`     | Clears all data from the FIFO                    |
| `IsReady()` | `bool`             | Returns `true` if there is data available to read |
| `Count()`   | `size_t`           | Returns the number of bytes currently in the FIFO |
| `Pop()`     | `Result<uint8>`    | Removes and returns the oldest byte (`Value()`) with a status (`IsOk()`, `Error()`) |

---

## Choosing Between SerialBuffer and SerialFIFO

Both classes provide the same high-level functionality. The difference is the underlying storage:

- **SerialBuffer** uses `RingBuffer` -- a lightweight ring buffer indexed by `uint16`. Suitable when you need a simple, minimal byte buffer.
- **SerialFIFO** uses `StaticFifo` -- a FIFO queue with `size_t` indexing and typed push/pop. Suitable when you want consistency with other FIFO-based data pipelines in your project.

Choose whichever matches the buffer primitive already used in your codebase.
