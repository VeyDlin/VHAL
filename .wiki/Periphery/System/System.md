# System

Static class providing platform-independent system services: timing, delays, device identification, critical sections, and error handling. All methods are static — no instances needed.

Platform-specific implementations (STM32, ENS, ESP32) provide the low-level details; the API remains the same across all platforms.

## Initialization

```cpp
System::Init();
```

Call once during BSP init, before using `DelayUs()`. Initializes the DWT cycle counter (on ARM Cortex-M) for microsecond-precision delays.

## Timing

### Tick Counter

The tick counter increments on every SysTick interrupt. Call `System::TickHandler()` from the SysTick ISR.

```cpp
uint64 ticks = System::GetTick();   // raw tick count
uint64 ms    = System::GetMs();     // ticks converted to milliseconds
uint32 core  = System::GetCoreTick(); // CPU cycle counter (DWT->CYCCNT on ARM)
uint32 freq  = System::GetCoreClock(); // core clock frequency in Hz
```

| Method | Resolution | Overflow | Use case |
|--------|-----------|----------|----------|
| `GetTick()` | 1 ms (typical) | ~584 million years (uint64) | General timing, timeouts |
| `GetMs()` | 1 ms | Same as GetTick | Human-readable timestamps |
| `GetCoreTick()` | 1 / core_freq | ~67 sec at 64 MHz (uint32) | Microsecond profiling |
| `GetCoreClock()` | — | — | Calculating delays, baud rates |

### Delays

```cpp
System::DelayMs(500);  // millisecond delay
System::DelayUs(100);  // microsecond delay (DWT-based on ARM)
```

`DelayMs()` is RTOS-aware — if a `rtosDelayMsHandle` is set and the scheduler is running, it yields the CPU to other threads instead of busy-waiting:

```cpp
// Set up in Application init:
System::rtosDelayMsHandle = [](auto delay) {
    if (OS::RTOS::IsSchedulerRun()) {
        OS::RTOS::Sleep(std::chrono::milliseconds(delay));
        return true;
    }
    return false;
};
```

`DelayUs()` always busy-waits using the DWT cycle counter. It includes a safety timeout (SysTick-based) to avoid infinite loops if the cycle counter malfunctions. On platforms without DWT, falls back to `DelayMs(delay / 1000)`.

## Device ID

```cpp
System::DeviceId id = System::GetDeviceId();

// Access as words (3 x 32-bit)
uint32 word0 = id.words[0];

// Access as bytes (12 bytes)
uint8 byte0 = id.bytes[0];

// Access structured fields
uint8 waferX = id.fields.waferX;
uint32 unique = id.fields.unique;
```

Returns the unique device identifier from the MCU. On STM32, reads the UID registers (`LL_GetUID_Word0/1/2`). The `DeviceId` union provides three views of the same 96-bit data.

## Critical Sections

```cpp
System::CriticalSection(true);   // disable interrupts
// ... atomic operation ...
System::CriticalSection(false);  // re-enable interrupts
```

For RTOS contexts, prefer the RAII wrapper `OS::CriticalSection` instead — it automatically re-enables interrupts when the scope ends.

## Interrupt Detection

```cpp
if (System::IsInterrupt()) {
    // Currently in an ISR — use ISR-safe APIs only
}
```

Used internally by RTOS wrappers to select between ISR and thread variants of FreeRTOS calls. On ARM, checks the IPSR register.

## Error Handling

### SystemAssert / SystemAbort

```cpp
SystemAssert(pointer != nullptr);               // assert with auto-generated message
SystemAssert(value > 0, "Value must be positive"); // assert with custom message
SystemAbort();                                    // unconditional abort
SystemAbort("Something went wrong");             // abort with message
```

In debug builds (`USE_FULL_ASSERT`), `SystemAssert` logs the file, line, and message via `CriticalError()`. In release builds, it simply halts if the condition is false.

### CriticalError

```cpp
System::CriticalError("Buffer overflow", __FILE__, __LINE__);
```

Calls the `criticalErrorHandle` callback (if set), logs to console (if `VHAL_SYSTEM_CONSOLE` enabled), then calls `Abort()` which disables interrupts and halts in an infinite loop.

Set the handler in your application init to save errors to flash or log:

```cpp
System::criticalErrorHandle = [](auto message, auto file, auto line) {
    // Save to flash, send over UART, etc.
};
```

## Console (printf) Support

When `VHAL_SYSTEM_CONSOLE` is defined, `System` provides a `Console` instance and redirects C `_write`/`_read` syscalls through it, enabling `printf()`:

```cpp
System::SetWriteHandler([](const char* str, size_t size) {
    BSP::consoleSerial.WriteArray((uint8*)str, size);
});

// Now printf works:
printf("Hello, value = %d\r\n", value);

// Or use the console directly:
System::console << "Temperature: " << temp << Console::endl;
```

## System Reset

```cpp
System::Reset(); // triggers MCU reset (NVIC_SystemReset on ARM)
```
