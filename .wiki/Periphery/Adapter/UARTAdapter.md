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
| `Read(uint32 size)` | `Result<DataType>` | Read a single value |
| `ReadArray(DataType* buffer, uint32 size)` | `ResultStatus` | Read an array of values |

## Asynchronous API

| Method | Return | Description |
|--------|--------|-------------|
| `WriteAsync(DataType& data)` | `ResultStatus` | Non-blocking send |
| `WriteArrayAsync(DataType* buffer, uint32 size)` | `ResultStatus` | Non-blocking array send |
| `WriteStringAsync(char* string)` | `ResultStatus` | Non-blocking string send |
| `ReadAsync(DataType& data)` | `ResultStatus` | Non-blocking read |
| `ReadArrayAsync(DataType* buffer, uint32 size)` | `ResultStatus` | Non-blocking array read |

## Configuration & Control

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `void` | Apply configuration and reinitialize |
| `SetTimeout(uint32 val)` | `void` | Set timeout for synchronous operations (ms) |
| `SetContinuousAsyncTxMode(bool)` | `ResultStatus` | Enable/disable continuous TX interrupt mode |
| `SetContinuousAsyncRxMode(bool)` | `ResultStatus` | Enable/disable continuous RX interrupt mode |
| `GetLastRxData()` | `uint16` | Get last received byte |
| `GetRxState()` | `ResultStatus` | Get RX state (`ready`, `busy`, `error`) |
| `GetTxState()` | `ResultStatus` | Get TX state |
| `AbortReceive()` | `void` | Abort active receive operation |
| `AbortTransmit()` | `void` | Abort active transmit operation |
| `AbortAll()` | `void` | Abort all operations |
| `IrqHandler()` | `void` | Must be called from the UART IRQ handler |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(Irq irqType)>` | Called when TX/RX completes asynchronously |
| `onError` | `std::function<void(Error errorType)>` | Called on communication error |

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
