# Thread

Base class for RTOS tasks. Inherit from `ThreadStatic<StackSize>` and override `Execute()` to define the task body.

## ThreadStatic vs Thread

| Type | Stack allocation | Use case |
|------|-----------------|----------|
| `ThreadStatic<N>` | Static (`std::array` on the object) | Most tasks — no heap, no fragmentation |
| `Thread<N>` | Dynamic (allocated by FreeRTOS) | When stack size must be decided at runtime |

`N` is the stack size in **words** (not bytes). On a 32-bit MCU, 128 words = 512 bytes.

| `N` | Enum | Typical use |
|-----|------|-------------|
| 128 | `StackDepth::minimal` | Simple GPIO toggle, watchdog reset |
| 256 | `StackDepth::medium` | UART/SPI communication, basic logic |
| 512 | `StackDepth::big` | String processing, complex state machines |
| 1024 | `StackDepth::biggest` | Heavy computation, large local buffers |

If your task crashes or behaves erratically, try increasing the stack size first.

## Creating a Task

A task is a class that inherits `ThreadStatic<N>` and overrides `Execute()`. The `Execute()` method is the task's entry point — it should contain an infinite loop, because returning from it is undefined behavior.

```cpp
class BlinkTask: public OS::ThreadStatic<128> {
public:
    void Execute() override {
        BSP::ledPin.SetParameters({ AGPIO::Mode::Output });

        while (true) {
            BSP::ledPin.Toggle();
            Sleep(500ms);
        }
    }
};
```

Register the task in `Application`:

```cpp
BlinkTask blinkTask;
RTOS::CreateThread(blinkTask, ThreadPriority::normal, "blink");
RTOS::Start();
```

## Sleep

Suspends the current thread for a duration. Other threads continue running.

```cpp
Sleep(100ms);
Sleep(2s);
Sleep(50ms);
```

Uses C++ chrono literals. The actual resolution depends on the RTOS tick rate (typically 1ms).

## SleepUntil

For periodic tasks where you need consistent timing regardless of how long the work takes:

```cpp
void Execute() override {
    while (true) {
        auto start = /* ... */;
        DoWork(); // takes variable time
        SleepUntil(10ms); // next wakeup is exactly 10ms from last wakeup
    }
}
```

`Sleep(10ms)` would drift because the delay starts *after* `DoWork()` finishes. `SleepUntil(10ms)` compensates for the execution time and maintains a fixed period.

## Signals

Signals are lightweight notifications between threads or from ISRs to threads. They use FreeRTOS task notifications internally — no extra objects needed.

### Sending a signal (from another thread or ISR)

```cpp
blinkTask.Signal();
```

This is ISR-safe — the wrapper automatically detects if called from an interrupt and uses the appropriate FreeRTOS API (`xTaskNotifyFromISR` with `portYIELD_FROM_ISR`).

### Waiting for a signal

```cpp
void Execute() override {
    while (true) {
        WaitForSignal();          // blocks indefinitely
        BSP::ledPin.Toggle();
    }
}
```

### With timeout and mask

```cpp
tTaskEventMask bits = WaitForSignal(500ms, 0b00000011);
if (bits & 0b01) {
    // event A happened
}
if (bits & 0b10) {
    // event B happened
}
```

The mask allows selective signaling — different bits can represent different events. The default mask is `0b010101010`.

## Suspend / Resume

Manually pause and resume a thread:

```cpp
task.Suspend(); // pauses the thread immediately
// ...
task.Resume();  // resumes from where it was suspended
```

`Resume()` is ISR-safe — it uses `xTaskResumeFromISR` when called from an interrupt handler.

## Complete Example

A task that reads a sensor every second and sends data through a UART:

```cpp
class SensorTask: public OS::ThreadStatic<256> {
public:
    void Execute() override {
        BSP::consoleSerial.SetParameters({ .baudRate = 115200 });

        while (true) {
            float temperature = ReadSensor();

            char buffer[32];
            int len = snprintf(buffer, sizeof(buffer), "T=%.1f\r\n", temperature);
            BSP::consoleSerial.WriteArray((uint8*)buffer, len);

            SleepUntil(1s);
        }
    }

private:
    float ReadSensor() {
        // Read ADC, convert to temperature
        return 25.0f;
    }
};
```
