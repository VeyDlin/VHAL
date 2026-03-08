# Event

Event flag group for inter-thread and ISR-to-thread signaling. Unlike thread signals (which are per-thread), events are standalone objects that multiple threads can wait on or signal.

## When to Use Events vs Thread Signals

| Feature | Thread `Signal()` | `Event` |
|---------|-------------------|---------|
| Ownership | Tied to a specific thread | Standalone object, shared by anyone |
| Multiple waiters | No — only the owning thread can wait | Yes — multiple threads can wait on the same event |
| Bit masking | Yes | Yes |
| ISR-safe | Yes | Yes |
| Extra object needed | No | Yes |

Use thread signals for simple "wake this specific thread" patterns. Use events when multiple threads need to synchronize on shared conditions.

## Usage

```cpp
OS::Event dataReady(100ms, OS::Event::defaultMask);

// Producer thread or ISR:
dataReady.Signal();

// Consumer thread:
tEventBits bits = dataReady.Wait();
if (bits) {
    // Event was signaled within timeout
} else {
    // Timeout expired
}
```

## Constructor

```cpp
Event(std::chrono::milliseconds delay, tEventBits maskBits);
```

- `delay` — default wait timeout
- `maskBits` — which bits to signal by default (8-bit mask, default `0b11111111`)

## API

| Method | Description |
|--------|-------------|
| `Signal()` | Set the event flag bits. ISR-safe — automatically uses `xEventGroupSetBitsFromISR` when called from an interrupt |
| `Wait(mode, mask)` | Block until flags are set or timeout. Returns the bits that were set. Flags are cleared after `Wait()` returns |
| `SetTimeout(duration)` | Change the default wait timeout |
| `SetMaskBits(mask)` | Change the default signal mask |

## Wait Modes

| Mode | Description |
|------|-------------|
| `EventMode::waitAnyBits` | Return when **any** of the masked bits are set (default) |
| `EventMode::waitAllBits` | Return only when **all** masked bits are set simultaneously |

## Example: Multiple Producers, One Consumer

```cpp
OS::Event sensorEvents(500ms, 0b11);

class TempSensor: public OS::ThreadStatic<128> {
    void Execute() override {
        while (true) {
            ReadTemperature();
            sensorEvents.SetMaskBits(0b01);
            sensorEvents.Signal();
            Sleep(1s);
        }
    }
};

class HumiditySensor: public OS::ThreadStatic<128> {
    void Execute() override {
        while (true) {
            ReadHumidity();
            sensorEvents.SetMaskBits(0b10);
            sensorEvents.Signal();
            Sleep(2s);
        }
    }
};

class DisplayTask: public OS::ThreadStatic<256> {
    void Execute() override {
        while (true) {
            // Wait for BOTH sensors to report
            tEventBits bits = sensorEvents.Wait(EventMode::waitAllBits, 0b11);
            if (bits == 0b11) {
                UpdateDisplay();
            }
        }
    }
};
```

## Enable

Add to `VHALConfig.h`:

```cpp
#define VHAL_RTOS_EVENT
```
