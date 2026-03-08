# Trigger

Lightweight C++ trigger utilities for one-shot event detection: flag triggers, rising-edge detectors, and variable change observers.

## Table of Contents

- [Quick Start](#quick-start)
- [Trigger](#trigger-1)
- [EdgeTrigger](#edgetrigger)
- [TriggerObserver](#triggerobserver)
- [API Reference](#api-reference)

## Quick Start

```cpp
#include <Utilities/Trigger/Trigger.h>
#include <Utilities/Trigger/EdgeTrigger.h>
#include <Utilities/Trigger/TriggerObserver.h>

// One-shot flag
Trigger flag = false;
flag = true;
if (flag) {
    // Runs once, then flag auto-resets to false
}

// Rising-edge detection
EdgeTrigger edge;
edge = buttonPressed; // assign each loop iteration
if (edge) {
    // Runs once per false->true transition
}

// Variable change observer
int speed = 100;
TriggerObserver<int> obs(speed);
speed = 200;
if (obs) {
    // Runs once because speed changed from 100 to 200
}
```

## Trigger

A single-shot boolean flag. When checked via `if (trigger)` or `Is(true)`, the condition fires **once** if the internal state matches, then the state auto-inverts. To fire again, you must explicitly set the flag back.

### Usage

```cpp
Trigger flag = false;

// Main loop
while (true) {
    if (flag) {
        // Executes once when flag is true, then flag becomes false
        HandleEvent();
    }
}

// Somewhere else (e.g., interrupt, another task):
flag = true;  // Arms the trigger -- the if-block above will fire once
```

### Waiting for false

```cpp
Trigger flag = true;

while (true) {
    if (!flag) {
        // Executes once when flag is false, then flag becomes true
    }
}

flag = false; // Arms the trigger for the !flag check
```

### Reading state without triggering

Use `==`, `!=`, or `GetState()` to inspect the current state without consuming the trigger:

```cpp
Trigger flag = true;

if (flag == true) {
    // Non-destructive read -- does NOT reset the flag
}

bool current = flag.GetState(); // Also non-destructive
```

## EdgeTrigger

Detects a **rising edge** (false-to-true transition) on an externally assigned boolean condition. Unlike `Trigger`, you assign the condition value each iteration and `EdgeTrigger` tracks the transition internally.

### Usage

```cpp
EdgeTrigger trigger;

while (true) {
    bool error = CheckForError();
    trigger = error; // Update with current condition

    if (trigger) {
        // Fires once when error goes from false to true
        LogError();
    }
}
```

The trigger will not fire again until the condition returns to `false` and then becomes `true` again.

### Inspecting state

```cpp
EdgeTrigger trigger;
trigger = someCondition;

if (trigger == true) {
    // Current state is true (does not consume edge)
}

bool state = trigger.GetState(); // Returns current internal state
```

## TriggerObserver

A **generic change detector**. Monitors a variable by reference and fires once each time the value changes. Works with any type that supports `!=` and copy assignment.

### Usage

```cpp
int mode = 0;
TriggerObserver<int> modeChanged(mode);

while (true) {
    if (modeChanged) {
        // Fires once each time `mode` changes value
        ApplyNewMode(mode);
    }
}

// Elsewhere:
mode = 1; // Next check of modeChanged will fire
mode = 1; // No change -- will not fire
mode = 2; // Will fire again
```

### With custom types

```cpp
struct Config {
    int baudRate;
    bool operator!=(const Config& o) const { return baudRate != o.baudRate; }
};

Config cfg{9600};
TriggerObserver<Config> cfgChanged(cfg);

cfg.baudRate = 115200;
if (cfgChanged) {
    // Config changed -- reconfigure peripheral
}
```

### Using Event() directly

The `Event()` method is equivalent to the `bool` conversion operator:

```cpp
TriggerObserver<float> tempChanged(temperature);

if (tempChanged.Event()) {
    // Same as: if (tempChanged)
}
```

## API Reference

### Trigger

| Member | Description |
|---|---|
| `Trigger()` | Default constructor, state = `false` |
| `Trigger(bool val)` | Construct with initial state |
| `operator bool()` | Returns `true` once if state is `true`, then auto-inverts to `false` |
| `operator!()` | Returns `true` once if state is `false`, then auto-inverts to `true` |
| `operator=(bool val)` | Sets the internal state |
| `operator==(bool val)` | Non-destructive state comparison |
| `operator!=(bool val)` | Non-destructive state comparison |
| `GetState()` | Returns current state without triggering |
| `Is(bool wait)` | Returns `true` and inverts state if current state equals `wait` |

### EdgeTrigger

| Member | Description |
|---|---|
| `EdgeTrigger()` | Default constructor, both states = `false` |
| `EdgeTrigger(bool initial)` | Construct with initial state |
| `operator=(bool condition)` | Updates internal state (shifts current to last) |
| `operator bool()` | Returns `true` if rising edge detected (`!last && current`) |
| `operator!()` | Returns `true` if no rising edge |
| `operator==(bool val)` | Compares current state (non-destructive) |
| `operator!=(bool val)` | Compares current state (non-destructive) |
| `GetState()` | Returns current internal state |

### TriggerObserver\<Type\>

| Member | Description |
|---|---|
| `TriggerObserver()` | Default constructor (no variable bound) |
| `TriggerObserver(Type& val)` | Construct and bind to a variable by reference |
| `operator bool()` | Returns `true` once if the monitored value changed since last check |
| `operator!()` | Returns `true` if the monitored value has not changed |
| `Event()` | Explicit form of `operator bool()` -- returns `true` and updates snapshot if value changed |
