# Buttons

Event-driven button handling library for embedded systems with debouncing, multi-click detection, button combinations, and sequenced input recognition.

## Table of Contents

- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [Button](#button)
- [Click](#click)
- [Action](#action)
- [Event](#event)
- [Core](#core)
- [Helper Functions](#helper-functions)
- [API Reference](#api-reference)

## Quick Start

```cpp
#include <Utilities/Hardware/Buttons/Core.h>

using namespace std::chrono_literals;

// 1. GPIO adapters (platform-specific)
AGPIO gpioA(GPIOA, 0);
AGPIO gpioB(GPIOA, 1);

// 2. Wrap GPIOs as buttons
Buttons::Button btnA(gpioA);
Buttons::Button btnB(gpioB);

// 3. Define a click (single button)
Buttons::ClickDefault clickA(btnA);

// 4. Define an event: 3 short presses of button A
Buttons::EventDefault triplePress({
    Buttons::MakeClickAction(clickA, Buttons::Action(Buttons::Action::ClickType::Short, 3))
});
triplePress.SetOnDoneHandle([]() {
    // Triggered after 3 short presses
});

// 5. Create core and register
Buttons::Core<8> core;
core.AddEvent(triplePress);
core.EnableAll();

// 6. Call in your main loop
while (true) {
    core.ProcessEvents(10ms);
    osDelay(10);
}
```

## Architecture

The library follows a layered design where each layer builds on the previous:

```
Button  ->  Click  ->  Action + Click  ->  Event  ->  Core
(GPIO)     (combo)     (type & count)     (sequence)  (manager)
```

- **Button** reads a single GPIO pin and applies debounce filtering.
- **Click** groups one or more Buttons into a combination (all must be pressed simultaneously).
- **Action** defines what kind of press is expected (short/long/very long) and how many times.
- **Event** chains one or more Click+Action pairs into a sequence, with optional hold conditions and completion callbacks.
- **Core** owns the update loop, manages timing configuration, and processes all registered events.

## Button

Wraps an `AGPIO` reference. Assumes active-low logic (pressed = GND). Provides debounced state and edge detection.

```cpp
Buttons::Button btn(gpioAdapter);

btn.Update(currentTime);        // Call every tick
btn.SetDebounceTime(30ms);      // Default: 50ms

btn.IsPressed();                // true while held
btn.WasPressed();               // rising edge (just pressed)
btn.WasReleased();              // falling edge (just released)
btn.HasChanged();               // state changed this tick
```

## Click

Represents a single button or a combination of buttons that must all be pressed together. Tracks press duration and provides edge detection for the combination.

```cpp
// Single button click
Buttons::ClickDefault clickSingle(btnA);

// Multi-button combination (both must be held simultaneously)
Buttons::ClickDefault clickCombo({btnA, btnB});

// Optional press/release callbacks
clickSingle.SetOnPushHandle([]() { /* combo just pressed */ });
clickSingle.SetOnPullHandle([]() { /* combo just released */ });

// State queries
clickSingle.IsPressed();            // true while all buttons held
clickSingle.WasJustPressed();       // rising edge
clickSingle.WasJustReleased();      // falling edge
clickSingle.GetPressDuration();     // how long current/last press lasted
```

The template parameter `MaxButtons` (default 4) controls the maximum number of buttons in a combination:

```cpp
Buttons::Click<8> wideCombo({btn1, btn2, btn3, btn4, btn5});
```

`ClickDefault` is an alias for `Click<4>`.

## Action

Defines the expected click type and repetition count.

### Click Types

| Type          | Condition                                          |
|---------------|---------------------------------------------------|
| `Short`       | Duration < `shortClickThreshold` (default 500ms)  |
| `Long`        | Duration >= `shortClickThreshold` and < `longClickThreshold` |
| `VeryLong`    | Duration >= `longClickThreshold` (default 2000ms) |
| `Infinitely`  | Any duration (hold until released)                |
| `Other`       | Any duration                                      |

```cpp
// 1 short click
Buttons::Action shortPress(Buttons::Action::ClickType::Short, 1);

// 3 short clicks (e.g., triple-click)
Buttons::Action tripleShort(Buttons::Action::ClickType::Short, 3);

// 1 long press
Buttons::Action longPress(Buttons::Action::ClickType::Long, 1);

// 1 very long press
Buttons::Action veryLong(Buttons::Action::ClickType::VeryLong, 1);

// Fluent API
Buttons::Action custom;
custom.SetClickType(Buttons::Action::ClickType::Long).SetClicksCount(2);
```

## Event

An Event is a sequence of Click+Action pairs. When all pairs in the sequence are completed in order, the `onDone` callback fires. Events support both global and per-action hold conditions.

### Basic event (single action)

```cpp
Buttons::ClickDefault clickPower(powerBtn);

Buttons::EventDefault powerOff({
    Buttons::MakeClickAction(clickPower, Buttons::Action(Buttons::Action::ClickType::Long, 1))
});

powerOff.SetOnDoneHandle([]() { System::PowerOff(); });
powerOff.SetOnFailedHandle([]() { /* sequence failed or timed out */ });
```

### Multi-step sequence

```cpp
Buttons::ClickDefault clickA(btnA);
Buttons::ClickDefault clickB(btnB);

// Step 1: short press A, then Step 2: long press B
Buttons::EventDefault sequence({
    Buttons::MakeClickAction(clickA, Buttons::Action(Buttons::Action::ClickType::Short, 1)),
    Buttons::MakeClickAction(clickB, Buttons::Action(Buttons::Action::ClickType::Long, 1))
});
sequence.SetOnDoneHandle([]() { /* both steps completed */ });
```

### Global hold condition

Require a button to be held during the entire sequence:

```cpp
Buttons::EventDefault reset({
    Buttons::MakeClickAction(clickA, Buttons::Action(Buttons::Action::ClickType::Short, 5))
});
reset.WhileHolding(powerBtn);  // Power must be held throughout
```

### Per-action hold condition

Require a button to be held only during a specific action within the sequence:

```cpp
Buttons::EventDefault secretMenu({
    Buttons::MakeClickActionWithHold(
        clickHome,
        Buttons::Action(Buttons::Action::ClickType::Long, 1),
        {volumeDownBtn}  // Hold volume down during this step only
    ),
    Buttons::MakeClickAction(clickCombo,
        Buttons::Action(Buttons::Action::ClickType::Short, 3))
});
```

### Event states

```cpp
enum class State { Idle, WaitingForSequence, Completed, Failed };

event.GetState();       // Current state
event.IsEnabled();      // Whether event is being processed
event.Enable();         // Enable and reset
event.Disable();        // Stop processing
event.Reset();          // Reset to Idle without disabling
```

### Timing configuration

```cpp
event.SetTimeout(5000ms);                      // Max time between sequence steps
event.SetTimeThresholds(400ms, 1500ms);        // short/long boundary, long/verylong boundary
event.SetMultiClickGap(300ms);                 // Max gap between repeated clicks
```

The template parameter `MaxSequences` (default 8) controls the maximum number of steps per event. `EventDefault` is an alias for `Event<8>`.

## Core

Central manager that owns the update loop. Automatically collects all buttons from registered events and handles debounce updates.

```cpp
Buttons::Core<16> core;  // Template parameter = max events (default 16)
```

### Timing configuration

All timing fields are public and use `std::chrono::milliseconds`:

| Field                  | Default  | Description                              |
|------------------------|----------|------------------------------------------|
| `debounceTime`         | 50ms     | Contact bounce filter duration           |
| `shortClickThreshold`  | 500ms    | Clicks shorter than this are "short"     |
| `longClickThreshold`   | 2000ms   | Clicks longer than this are "very long"  |
| `sequenceTimeout`      | 5000ms   | Max time between sequence actions        |
| `multiClickGap`        | 300ms    | Max gap between repeated clicks          |

```cpp
using namespace std::chrono_literals;

core.debounceTime = 20ms;
core.shortClickThreshold = 400ms;
core.longClickThreshold = 1000ms;
core.sequenceTimeout = 3000ms;
core.multiClickGap = 250ms;
core.UpdateTimeSettings();  // Propagate to all registered buttons and events
```

### Event management

```cpp
core.AddEvent(event);           // Register event, returns false if full
core.RemoveEvent(event);        // Unregister event
core.EnableAll();               // Enable all registered events
core.DisableAll();              // Disable all registered events
core.GetEventCount();           // Number of registered events
core.GetMaxEvents();            // Template capacity
core.GetEvent(index);           // Access event by index (nullptr if out of range)
core.RegisterButton(button);    // Manually register a button for the update loop
```

### Processing

Call `ProcessEvents` periodically with the elapsed time since the last call:

```cpp
// In main loop
while (true) {
    core.ProcessEvents(10ms);
    osDelay(10);
}

// Or in an RTOS task
void ButtonTask(void*) {
    for (;;) {
        core.ProcessEvents(5ms);
        osDelay(5);
    }
}
```

## Helper Functions

Two free functions simplify the creation of `ClickAction` structs:

```cpp
// Basic click-action pair
auto ca = Buttons::MakeClickAction(click, action);

// Click-action pair with per-action hold conditions
auto ca = Buttons::MakeClickActionWithHold(click, action, {holdBtn1, holdBtn2});
```

## API Reference

### `Button`

| Method | Description |
|--------|-------------|
| `Button(AGPIO& gpio)` | Construct from GPIO adapter |
| `Update(ms currentTime)` | Update debounce state |
| `SetDebounceTime(ms time)` | Set debounce duration |
| `IsPressed() -> bool` | Currently pressed (debounced) |
| `WasPressed() -> bool` | Rising edge this tick |
| `WasReleased() -> bool` | Falling edge this tick |
| `HasChanged() -> bool` | State changed this tick |

### `Click<MaxButtons>`

| Method | Description |
|--------|-------------|
| `Click(Button& btn)` | Single button |
| `Click(initializer_list<ref<Button>>)` | Multi-button combination |
| `SetOnPushHandle(function<void()>)` | Callback on press |
| `SetOnPullHandle(function<void()>)` | Callback on release |
| `Update(ms currentTime)` | Update combination state |
| `IsPressed() -> bool` | All buttons held |
| `WasJustPressed() -> bool` | Rising edge |
| `WasJustReleased() -> bool` | Falling edge |
| `GetPressDuration() -> ms` | Current/last press duration |
| `ForEachButton(Func)` | Iterate over contained buttons |

### `Action`

| Method | Description |
|--------|-------------|
| `Action()` | Default: Short, 1 click |
| `Action(ClickType, uint8 count)` | Construct with type and count |
| `SetClickType(ClickType) -> Action&` | Set click type (fluent) |
| `SetClicksCount(uint8) -> Action&` | Set repetition count (fluent) |
| `GetClickType() -> ClickType` | Get click type |
| `GetClicksCount() -> uint8` | Get repetition count |
| `IsValidDuration(ms, ms short, ms long) -> bool` | Check if duration matches click type |

### `Event<MaxSequences>`

| Method | Description |
|--------|-------------|
| `Event(initializer_list<ClickAction<>>)` | Construct with sequence of click-actions |
| `Event(ClickAction<>&)` | Single click-action |
| `Enable()` | Enable and reset |
| `Disable()` | Disable processing |
| `Reset()` | Reset state to Idle |
| `WhileHolding(Button&)` | Add global hold condition |
| `WhileHolding(initializer_list<ref<Button>>)` | Add multiple global hold conditions |
| `SetOnDoneHandle(function<void()>)` | Callback on sequence completion |
| `SetOnFailedHandle(function<void()>)` | Callback on sequence failure |
| `SetTimeout(ms)` | Max time between sequence steps |
| `SetTimeThresholds(ms short, ms long)` | Click type boundaries |
| `SetMultiClickGap(ms)` | Max gap for multi-clicks |
| `GetState() -> State` | Current state |
| `IsEnabled() -> bool` | Whether enabled |
| `ForEachButton(Func)` | Iterate all buttons in event |

### `Core<MaxEvents>`

| Method | Description |
|--------|-------------|
| `AddEvent(EventDefault&) -> bool` | Register event |
| `RemoveEvent(EventDefault&) -> bool` | Unregister event |
| `EnableAll()` | Enable all events |
| `DisableAll()` | Disable all events |
| `ProcessEvents(ms deltaTime)` | Main update loop |
| `UpdateTimeSettings()` | Propagate timing to buttons/events |
| `GetEventCount() -> size_t` | Number of registered events |
| `GetMaxEvents() -> size_t` | Template capacity |
| `GetEvent(size_t) -> EventDefault*` | Access by index |
| `RegisterButton(Button&) -> bool` | Manual button registration |
