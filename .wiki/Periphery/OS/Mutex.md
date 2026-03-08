# Mutex

Mutual exclusion lock for protecting shared resources between threads. Built on FreeRTOS semaphores with priority inheritance.

## Usage

```cpp
OS::Mutex sharedMutex;

// In thread A:
if (sharedMutex.Lock(100ms)) {
    // Access shared resource safely
    sharedMutex.UnLock();
}

// In thread B:
if (sharedMutex.Lock(Mutex::waitForEver)) {
    // Will block until thread A releases
    sharedMutex.UnLock();
}
```

Always pair `Lock()` with `UnLock()`. If a thread that holds the mutex is deleted or crashes, the mutex remains locked forever.

## API

| Method | Description |
|--------|-------------|
| `Lock(timeout)` | Try to acquire the mutex. Returns `true` if locked within timeout, `false` if timeout expired |
| `UnLock()` | Release the mutex. Must be called from the same thread that locked it |

## Timeout Constants

| Constant | Description |
|----------|-------------|
| `Mutex::waitForEver` | Block indefinitely until the mutex becomes available |
| `Mutex::notWait` | Return immediately — `Lock()` returns `false` if already held |

## ISR Safety

Both `Lock()` and `UnLock()` are ISR-safe — the wrapper detects if called from an interrupt handler and uses the appropriate FreeRTOS API (`xSemaphoreTakeFromISR` / `xSemaphoreGiveFromISR`). However, using mutexes from ISRs is generally discouraged — prefer signals or events for ISR-to-thread communication.

## When to Use

| Scenario | Use |
|----------|-----|
| Protect a shared peripheral (e.g., SPI bus used by multiple tasks) | `Mutex` |
| Protect a few register writes that must be atomic | `CriticalSection` |
| Wake a thread from an ISR | Thread `Signal()` or `Event` |

## Example: Shared SPI Bus

```cpp
OS::Mutex spiBusMutex;

class SensorTask: public OS::ThreadStatic<256> {
    void Execute() override {
        while (true) {
            if (spiBusMutex.Lock(Mutex::waitForEver)) {
                BSP::spi.WriteArray(cmd, sizeof(cmd));
                BSP::spi.ReadArray(data, sizeof(data));
                spiBusMutex.UnLock();
            }
            Sleep(100ms);
        }
    }
};

class DisplayTask: public OS::ThreadStatic<256> {
    void Execute() override {
        while (true) {
            if (spiBusMutex.Lock(Mutex::waitForEver)) {
                BSP::spi.WriteArray(frameBuffer, sizeof(frameBuffer));
                spiBusMutex.UnLock();
            }
            Sleep(16ms); // ~60 FPS
        }
    }
};
```

## Enable

Add to `VHALConfig.h`:

```cpp
#define VHAL_RTOS_MUTEX
```
