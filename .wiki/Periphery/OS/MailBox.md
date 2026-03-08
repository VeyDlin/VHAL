# MailBox

Typed message queue for passing data between threads or from ISRs to threads. Built on FreeRTOS queues with type safety via C++ templates.

## Template Parameters

```cpp
OS::MailBox<T, size>
```

- `T` — message type (any copyable type: struct, int, pointer, etc.)
- `size` — queue depth, how many items can be buffered (default: 1)

The internal buffer is statically allocated (`std::array<uint8_t, size * sizeof(T)>`), so no heap allocation.

## Usage

```cpp
struct SensorData {
    float temperature;
    float humidity;
    uint32_t timestamp;
};

OS::MailBox<SensorData, 4> sensorQueue;

// Producer thread:
SensorData data = { 25.5f, 60.0f, System::GetTick() };
sensorQueue.Put(data, 100ms);

// Consumer thread:
SensorData received;
if (sensorQueue.Get(received, MailBox<SensorData, 4>::waitForEver)) {
    ProcessData(received);
}
```

## API

| Method | Description |
|--------|-------------|
| `Put(item, timeout)` | Copy an item into the queue. Returns `true` if placed within timeout, `false` if queue is full and timeout expired |
| `Get(item, timeout)` | Copy an item out of the queue (FIFO). Returns `true` if received within timeout, `false` if queue is empty and timeout expired |

Both methods are ISR-safe — the wrapper automatically detects interrupt context and uses `xQueueSendFromISR` / `xQueueReceiveFromISR`.

## Timeout Constants

| Constant | Description |
|----------|-------------|
| `MailBox::waitForEver` | Block until space is available (Put) or item arrives (Get) |
| `MailBox::notWait` | Return immediately if queue is full/empty |

## Choosing Queue Depth

| Depth | Use case |
|-------|----------|
| 1 | Latest-value pattern — producer overwrites, consumer reads most recent |
| 4-8 | Buffered stream — producer and consumer run at different rates |
| 16+ | High-throughput — bursty producer, batch consumer |

If the queue overflows (`Put` fails), you're either producing too fast or consuming too slow. Increase the depth or adjust timing.

## Example: ISR to Thread Communication

```cpp
OS::MailBox<uint8_t, 64> uartRxQueue;

// In UART ISR callback:
BSP::serial.onInterrupt = [](AUART::Irq irq) {
    if (irq == AUART::Irq::Rx) {
        uint8_t byte = BSP::serial.ReadByte();
        uartRxQueue.Put(byte);  // ISR-safe
    }
};

// In processing thread:
class CommTask: public OS::ThreadStatic<256> {
    void Execute() override {
        uint8_t byte;
        while (true) {
            if (uartRxQueue.Get(byte, MailBox<uint8_t, 64>::waitForEver)) {
                ProcessByte(byte);
            }
        }
    }
};
```

## Example: Struct Passing Between Tasks

```cpp
struct Command {
    enum Type { Start, Stop, SetSpeed };
    Type type;
    int32_t value;
};

OS::MailBox<Command, 8> commandQueue;

// UI thread:
commandQueue.Put(Command{ Command::SetSpeed, 1500 }, 10ms);

// Motor control thread:
Command cmd;
if (commandQueue.Get(cmd, 100ms)) {
    switch (cmd.type) {
        case Command::Start:    MotorStart(); break;
        case Command::Stop:     MotorStop(); break;
        case Command::SetSpeed: MotorSetRPM(cmd.value); break;
    }
}
```

## Enable

Add to `VHALConfig.h`:

```cpp
#define VHAL_RTOS_MAILBOX
```
