# Timer

Software timer built on top of a thread. Fires `Execute()` (or an `onTick` callback) after a configurable interval. Supports one-shot and periodic (auto-reload) modes.

Unlike FreeRTOS software timers, VHAL timers are full threads — they can call blocking APIs (`Sleep`, `WaitForSignal`, `Mutex::Lock`, etc.) inside the timer callback.

## Variants

| Class | Stack | Callback style |
|-------|-------|----------------|
| `TimerStatic<N>` | Static | Override `Execute()` |
| `Timer<N>` | Dynamic | Override `Execute()` |
| `TimerHandleStatic<N>` | Static | Set `onTick` callback (no subclassing needed) |
| `TimerHandle<N>` | Dynamic | Set `onTick` callback (no subclassing needed) |

## Inheritance-Based Timer

Subclass and override `Execute()`:

```cpp
class HeartbeatTimer: public OS::TimerStatic<128> {
public:
    void Execute() override {
        BSP::ledPin.Toggle();
    }
};

// Setup:
HeartbeatTimer heartbeat;
heartbeat.interval = 500ms;
heartbeat.autoReload = true;

RTOS::CreateThread(heartbeat, ThreadPriority::normal, "heartbeat");
// After RTOS::Start(), from another thread:
heartbeat.Start();
```

## Callback-Based Timer

No subclassing — set the `onTick` lambda:

```cpp
OS::TimerHandleStatic<128> watchdogTimer;
watchdogTimer.interval = 5s;
watchdogTimer.autoReload = true;
watchdogTimer.onTick = []() {
    BSP::watchdog.Reset();
};

RTOS::CreateThread(watchdogTimer, ThreadPriority::realtime, "wdt");
// After RTOS::Start(), from another thread:
watchdogTimer.Start();
```

## Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `interval` | `std::chrono::milliseconds` | `1s` | Time between ticks |
| `autoReload` | `bool` | `false` | `true` = periodic, `false` = one-shot (stops after first tick) |

## Methods

| Method | Description |
|--------|-------------|
| `Start()` | Start the timer. If already running, does nothing |
| `Stop()` | Stop the timer. The thread suspends on the next tick cycle |
| `Restart()` | Reset the interval counter without stopping. Use when you want to "kick" a running timer back to the beginning of its interval |

## One-Shot Timer

With `autoReload = false`, the timer fires once and then stops automatically:

```cpp
OS::TimerHandleStatic<128> delayedAction;
delayedAction.interval = 3s;
delayedAction.autoReload = false;
delayedAction.onTick = []() {
    BSP::motor.Enable();
};
// ...
delayedAction.Start(); // motor enables after 3 seconds, timer stops
```

## How It Works Internally

The timer is a thread that runs an infinite loop:
1. Sleep for `interval`
2. Check state (Run / Stopped / Restart)
3. If Run: call `Execute()`, then either stop (one-shot) or loop (auto-reload)
4. If Stopped: suspend the thread until `Start()` resumes it
5. If Restart: reset to Run state and loop

Because it's a real thread, the timer respects priorities and can be preempted by higher-priority tasks. The interval accuracy depends on the RTOS tick resolution and the task priority.

## Enable

Add to `VHALConfig.h`:

```cpp
#define VHAL_RTOS_TIMER
```
