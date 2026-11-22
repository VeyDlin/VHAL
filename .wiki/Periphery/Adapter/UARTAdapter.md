# UARTAdapter

Abstract adapter for UART/USART peripherals. Provides synchronous and asynchronous byte-oriented communication with configurable baud rate, parity, stop bits, and flow control.

Header: `#include <Adapter/UARTAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class UARTAdapter : public IAdapter;
```

`HandleType` — platform-specific UART register structure (e.g., `USART_TypeDef` on STM32, `CMSDK_UART_TypeDef` on ENS).

## Parameters

```cpp
struct Parameters {
    uint32 baudRate = 115200;
    StopBits stopBits = StopBits::B1;
    Parity parity = Parity::None;
    Mode mode = Mode::TxRx;
    FlowControl flowControl = FlowControl::None;
    OverSampling overSampling = OverSampling::B16;
};
```

| Field | Options | Default | Description |
|-------|---------|---------|-------------|
| `baudRate` | any `uint32` | `115200` | Communication speed in bits/second |
| `stopBits` | `B1`, `B2` | `B1` | Number of stop bits |
| `parity` | `None`, `Even`, `Odd` | `None` | Parity check mode |
| `mode` | `Tx`, `Rx`, `TxRx` | `TxRx` | Direction of communication |
| `flowControl` | `None`, `Rts`, `Cts`, `RtsCts` | `None` | Hardware flow control |
| `overSampling` | `B8`, `B16` | `B16` | Oversampling ratio |

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `Irq` | `Tx`, `Rx` | Interrupt source type |
| `Error` | `None`, `Parity`, `Noise`, `Frame`, `Overrun` | Error types |

## Synchronous API

| Method | Return | Description |
|--------|--------|-------------|
| `Write(DataType data)` | `ResultStatus` | Send a single value |
| `WriteArray(DataType* buffer, uint32 size)` | `ResultStatus` | Send an array of values |
| `WriteString(char* string)` | `ResultStatus` | Send a null-terminated string |
| `Read<DataType>()` | `Result<DataType>` | Read one value of `DataType` (`sizeof(DataType)` bytes). For several values use `ReadArray()` |
| `ReadArray(DataType* buffer, uint32 size)` | `ResultStatus` | Read an array of values |

## Asynchronous API

| Method | Return | Description |
|--------|--------|-------------|
| `WriteAsync(DataType& data)` | `ResultStatus` | Non-blocking send |
| `WriteArrayAsync(DataType* buffer, uint32 size)` | `ResultStatus` | Non-blocking array send |
| `WriteStringAsync(char* string)` | `ResultStatus` | Non-blocking string send |
| `ReadAsync(DataType& data)` | `ResultStatus` | Non-blocking read of one value (`sizeof(DataType)` bytes) |
| `ReadArrayAsync(DataType* buffer, uint32 size)` | `ResultStatus` | Non-blocking array read |

Every asynchronous call returns `invalidParameter` for a size of 0 (a `WriteStringAsync("")` included), and sends or receives nothing. The synchronous calls keep their behaviour.

## Configuration & Control

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Apply configuration and reinitialize |
| `SetTimeout(uint32 val)` | `void` | Set timeout for synchronous operations (ms) |
| `SetContinuousAsyncTxMode(bool)` | `ResultStatus` | Continuous TX. Returns `notSupported` on every STM32 port and on the base class |
| `SetContinuousAsyncRxMode(bool)` | `ResultStatus` | Enable/disable continuous RX interrupt mode |
| `GetLastRxData()` | `uint16` | Get last received byte |
| `GetRxState()` | `ResultStatus` | Get RX state (`ready`, `busy`, `error`) |
| `GetTxState()` | `ResultStatus` | Get TX state |
| `GetRxDataCounter()` | `uint32` | Bytes received by the current asynchronous read. Counts past 64 KB |
| `GetTxDataCounter()` | `uint32` | Bytes sent by the current asynchronous write. Counts past 64 KB |
| `AbortReceive()` | `void` | Abort active receive operation |
| `AbortTransmit()` | `void` | Abort active transmit operation |
| `AbortAll()` | `void` | Abort all operations |
| `IrqHandler()` | `void` | Must be called from the UART IRQ handler |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(Irq irqType)>` | Called when TX/RX completes asynchronously |
| `onError` | `std::function<void(Error errorType)>` | Called on communication error |

Replace a callback while its interrupt may run with its setter: `SetOnInterrupt(handler)` and `SetOnError(handler)`. See [Changing a callback while its interrupt may run](IAdapter.md#changing-a-callback-while-its-interrupt-may-run).

## ESP32 Port

`AUART` is `UARTAdapterESP` (`Periphery/Adapter/Port/ESP32/Common/UARTAdapterESP.h`), built on the ESP-IDF UART driver. `HandleType` is `void`.

```cpp
AUART(uart_port_t uartPort, int tx, int rx, int rts = UART_PIN_NO_CHANGE, int cts = UART_PIN_NO_CHANGE, uint32 bufSize = 1024);
```

- `bufSize` is the driver RX and TX ring buffer size in bytes.
- `Parameters::mode` and `Parameters::overSampling` are not applied on ESP32. `baudRate`, `stopBits`, `parity` and `flowControl` are applied.
- `IrqHandler()` does nothing on ESP32. The driver handles its own interrupts.
- `SetContinuousAsyncTxMode()` returns `notSupported`.
- `ReadArrayAsync` (`ReadAsync`, `ReadArrayAsync`):
  - Returns `notAvailable` while continuous RX mode is on.
  - Returns `busy` while a read is in progress.
  - Returns `noInit` before `SetParameters()` has installed the driver.
  - Fills the buffer in the background. `onInterrupt(Irq::Rx)` is called once, when the buffer is full.
- Continuous RX mode (`SetContinuousAsyncRxMode(true)`): `onInterrupt(Irq::Rx)` is called for every received byte. Read the byte with `GetLastRxData()` in the callback. `SetContinuousAsyncRxMode(true)` returns `busy` while an asynchronous read is in progress.
- RX callbacks and `onError` run in a background FreeRTOS task (`uart_rx`), not in an interrupt context. The task is created on first use and stopped when `SetParameters()` reinitializes the port.
- `WriteArrayAsync` and the other asynchronous writes copy the data into the driver TX buffer and call `onInterrupt(Irq::Tx)` from the calling context before returning.
- `onError` reports `Error::Overrun` (FIFO overflow or buffer full), `Error::Frame` and `Error::Parity`. `Error::Noise` is not reported.
- Synchronous `Read` returns `timeout` when fewer bytes than requested arrive within the timeout. Synchronous `Write` returns `timeout` when the TX buffer does not drain within the timeout.

## STM32 Ports

`AUART` is `UARTAdapterF4`, `UARTAdapterG0` or `UARTAdapterG4`, with `HandleType` = `USART_TypeDef`.

- `SetParameters()` waits for the transmitter (TEACK) and the receiver (REACK) that the `mode` enables, up to `SetTimeout()`, and returns `timeout` when one does not come. A receive-only port waits for REACK alone.
- Continuous TX (`SetContinuousAsyncTxMode(true)`) returns `notSupported`. Continuous RX is supported.
- Transfers above 64 KB are counted in full: the counters are `uint32`.

### Shared vector on G0

USART3, USART4 and LPUART1 share `USART3_4_LPUART1_IRQn`. The application's handler for that vector calls `IrqHandler()` of every UART adapter on it. Each adapter reads the flags and the interrupt enables of its own USART only, so the call that belongs to another USART does nothing. No adapter needs to know which USART fired.

## BSP Configuration Example (STM32)

```cpp
// BSP.h
class BSP {
public:
    static AUART consoleSerial;
};

// BSP.cpp
AUART BSP::consoleSerial = { USART1 };

void BSP::InitAdapterPeripheryEvents() {
    consoleSerial.beforePeripheryInit = []() {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

        AGPIO::AlternateInit({ GPIOA, 9,  1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh }); // TX
        AGPIO::AlternateInit({ GPIOA, 10, 1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh }); // RX

        NVIC_SetPriority(USART1_IRQn, 0);
        NVIC_EnableIRQ(USART1_IRQn);

        return ResultStatus::ok;
    };
}

// DeviceIrq.cpp
void USART1_IRQHandler() {
    BSP::consoleSerial.IrqHandler();
}
```

## Usage Example

```cpp
// Configure
BSP::consoleSerial.SetParameters({
    .baudRate = 9600,
    .stopBits = AUART::StopBits::B1,
    .parity = AUART::Parity::None,
    .mode = AUART::Mode::TxRx
});

// Synchronous write
BSP::consoleSerial.WriteString("Hello VHAL\r\n");

// Asynchronous read with callback
BSP::consoleSerial.onInterrupt = [](AUART::Irq irq) {
    if (irq == AUART::Irq::Rx) {
        auto data = BSP::consoleSerial.GetLastRxData();
        // process received byte
    }
};
BSP::consoleSerial.SetContinuousAsyncRxMode(true);
```
