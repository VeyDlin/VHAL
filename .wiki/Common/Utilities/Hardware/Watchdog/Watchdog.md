# Watchdog

A software watchdog timer built on top of the VHAL RTOS `Timer` infrastructure. It executes a callback once when the timer expires without being restarted, providing a simple mechanism to detect hangs or missed deadlines.

---

## Table of Contents

- [Quick Start](#quick-start)
- [How It Works](#how-it-works)
- [API Reference](#api-reference)
- [Usage Examples](#usage-examples)

---

## Quick Start

```cpp
#include <Utilities/Hardware/Watchdog/Watchdog.h>

// Create a watchdog with 512 bytes of stack for its RTOS thread
Watchdog<512> wdt;

// Set the timeout interval
wdt.interval = 2000ms;

// Define the action taken when the watchdog expires
wdt.onEnd = []() {
    // Handle timeout: reset the system, log an error, etc.
    SystemReset();
};

// Start the watchdog
wdt.Start();

// In your main loop or periodic task, restart the watchdog before it expires
while (true) {
    DoWork();
    wdt.Restart(); // "feed" the watchdog
}
```

---

## How It Works

`Watchdog` inherits from `Timer<stackSize>`, which is an RTOS-based software timer running in its own thread. The key behavior:

1. When `Start()` is called, the timer begins counting down for the configured `interval`.
2. Calling `Restart()` resets the countdown, preventing expiration -- this is the "feed" or "kick" operation.
3. If the timer expires (i.e., `Execute()` is called), the `onEnd` callback fires **once**, and `autoReload` is set to `false` so the timer stops automatically.

This makes it a **one-shot** watchdog: once it fires, it stops. You must explicitly `Start()` it again if you want to re-arm it.

---

## API Reference

### Template Parameter

| Parameter   | Description                                      |
|-------------|--------------------------------------------------|
| `stackSize` | Stack size in bytes for the underlying RTOS thread |

### Public Members

| Member       | Type                    | Description                                          |
|--------------|-------------------------|------------------------------------------------------|
| `onEnd`      | `std::function<void()>` | Callback invoked when the watchdog timer expires      |
| `interval`   | `std::chrono::milliseconds` | Timeout period (inherited from `ITimer`, default `1s`) |
| `autoReload` | `bool`                  | Auto-repeat flag (inherited; set to `false` on expiry) |

### Public Methods (Inherited from `ITimer` / `Timer`)

| Method      | Signature        | Description                                                                 |
|-------------|------------------|-----------------------------------------------------------------------------|
| `Start()`   | `void Start()`   | Starts the watchdog countdown. No effect if already running.                |
| `Stop()`    | `void Stop()`    | Stops the watchdog. The `onEnd` callback will not fire.                     |
| `Restart()` | `void Restart()` | Resets the countdown to the full `interval`. Only works while running.      |

### Protected Methods (Override)

| Method      | Signature              | Description                                                    |
|-------------|------------------------|----------------------------------------------------------------|
| `Execute()` | `void Execute()` | Called on timeout. Invokes `onEnd` and disables auto-reload.    |

---

## Usage Examples

### System Reset on Timeout

```cpp
Watchdog<256> systemWdt;
systemWdt.interval = 5000ms;
systemWdt.onEnd = []() {
    NVIC_SystemReset();
};
systemWdt.Start();

// Feed periodically from a task
void PeriodicTask() {
    while (true) {
        ProcessData();
        systemWdt.Restart();
        RTOS::Delay(100ms);
    }
}
```

### Communication Timeout Detection

```cpp
Watchdog<256> comWdt;
comWdt.interval = 500ms;
comWdt.onEnd = [&]() {
    motor.Stop();
    SetErrorFlag(Error::CommLost);
};

void OnMessageReceived(const Message& msg) {
    ProcessMessage(msg);
    comWdt.Restart(); // reset on every valid message
}

// Arm the watchdog when communication begins
void StartCommunication() {
    comWdt.Start();
}
```

### Re-arming After Expiry

Since the watchdog is one-shot, you can re-arm it manually:

```cpp
Watchdog<256> wdt;
wdt.interval = 3000ms;
wdt.onEnd = [&]() {
    HandleTimeout();
    // Re-arm for the next cycle
    wdt.Start();
};
wdt.Start();
```
