# RTOS

Static class that controls the FreeRTOS scheduler — creates threads, starts execution, and provides system-level sleep.

## Starting the Scheduler

The typical startup sequence:

```cpp
void Application::Init() {
    // Create all threads before starting
    RTOS::CreateThread(mainTask, ThreadPriority::normal, "main");
    RTOS::CreateThread(commTask, ThreadPriority::high, "comm");

    // Start the scheduler — this never returns
    RTOS::Start();
}
```

After `RTOS::Start()`, the scheduler takes control. Any code after it will never execute. All initialization that doesn't require the scheduler must happen before `Start()`.

## Creating Threads

```cpp
// Static stack (preferred):
RTOS::CreateThread(myTask, ThreadPriority::normal, "taskName");

// With default priority and no name:
RTOS::CreateThread(myTask);
```

Parameters:
- `thread` — reference to a `ThreadStatic<N>` or `Thread<N>` instance
- `prior` — thread priority (default: `ThreadPriority::normal`)
- `pName` — debug name visible in FreeRTOS-aware debuggers (default: `NULL`)

## Priority Levels

Priorities are numeric values. Higher number = higher priority = runs first when multiple threads are ready.

| Priority | Value | Use case |
|----------|-------|----------|
| `idle` | 1 | Background maintenance, logging |
| `low` | 2 | Non-time-critical processing |
| `belowNormal` | 3 | Low-priority I/O |
| `normal` | 4 | Default for most application tasks |
| `aboveNormal` | 5 | Time-sensitive communication |
| `high` | 6 | Fast-response tasks (motor control, safety) |
| `realtime` | configMAX_PRIORITIES - 1 | Hard-realtime (watchdog, emergency stop) |

Use `realtime` sparingly — a thread at this priority that doesn't yield will starve all other threads.

## System Sleep

```cpp
// Sleep from any context (scheduler must be running)
RTOS::Sleep(100ms);
```

This is a static method — you can call it from anywhere. Inside a thread, prefer the instance method `Sleep()` inherited from `IThread` (same effect, but clearer intent).

## Scheduler State

```cpp
if (RTOS::IsSchedulerRun()) {
    // Scheduler is active, safe to use RTOS primitives
} else {
    // Still in pre-scheduler init, use busy-wait only
}
```

Useful in code that runs both during init (before `Start()`) and at runtime.

## Interrupt Handlers

Your BSP interrupt file must forward three handlers to the RTOS. Without these, context switching and timing will not work.

```cpp
extern "C" void PendSV_Handler() {
    RTOS::HandlePendSvInterrupt();
}

extern "C" void SVC_Handler() {
    RTOS::HandleSvcInterrupt();
}

extern "C" void SysTick_Handler() {
    System::TickHandler();              // VHAL system tick
    RTOS::HandleSysTickInterrupt();     // FreeRTOS tick
}
```

`SysTick_Handler` must call both — `System::TickHandler()` for VHAL's delay/tick infrastructure and `RTOS::HandleSysTickInterrupt()` for FreeRTOS scheduling.

> **Note:** Set SysTick and PendSV to the lowest interrupt priority in your BSP init, otherwise FreeRTOS assertions may fire:
> ```cpp
> NVIC_SetPriority(PendSV_IRQn, 3);
> NVIC_SetPriority(SysTick_IRQn, 3);
> ```

## System::DelayMs Bridge

If code uses `System::DelayMs()` both before and after the scheduler starts, you need to bridge it to the RTOS:

```cpp
System::rtosDelayMsHandle = [](auto delay) {
    if (RTOS::IsSchedulerRun()) {
        RTOS::Sleep(std::chrono::milliseconds(delay));
        return true;   // handled by RTOS
    }
    return false;       // fallback to busy-wait
};
```

Without this, `System::DelayMs()` uses a busy-wait loop even when the scheduler is running, wasting CPU time.
