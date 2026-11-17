# Buttons

Event-driven button handling library for embedded systems with debouncing, multi-click detection, button combinations, and sequenced input recognition.

## Table of Contents

- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [Pin Polarity](#pin-polarity)
- [Button](#button)
- [Click](#click)
- [Action](#action)
- [Event](#event)
- [Core](#core)
- [Helper Functions](#helper-functions)
- [Capacity and Errors](#capacity-and-errors)
- [Migrating from Earlier Versions](#migrating-from-earlier-versions)
- [API Reference](#api-reference)

## Quick Start

```cpp
#include <Utilities/Hardware/Buttons/Core.h>

using namespace std::chrono_literals;

// 1. GPIO adapters (platform-specific). Button A is active-low: pressed = pin low.
AGPIO gpioA(GPIOA, 0, true);
AGPIO gpioB(GPIOA, 1, true);

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

// 6. Call in your main loop, passing the time measured since the previous call
uint64 lastMs = System::GetMs();
while (true) {
    uint64 nowMs = System::GetMs();
    core.ProcessEvents(std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(nowMs - lastMs)));
    lastMs = nowMs;
    osDelay(10);
}
```

The main loop passes the measured elapsed time, not the nominal period. `osDelay(10)` can return later than 10 ms, and every timing in the library (debounce, click lengths, timeouts) is measured in the time you pass in.

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
- **Core** owns the update loop, manages timing configuration, and processes all registered events and standalone clicks.

## Pin Polarity

Pin polarity belongs to the `AGPIO`, not to the button. Declare an active-low button with `inversion = true`:

```cpp
AGPIO buttonA = { 37, true };   // pressed = pin low (external pull-up)
Buttons::Button btnA(buttonA);  // IsPressed() is true while the pin is low
```

`Button` uses the logical level (`AGPIO::GetState()`), which already has the inversion applied.

**Behaviour change:** earlier versions applied the inversion a second time inside `Button`. A button declared with `inversion = false` on active-low wiring was read correctly, and one declared with `inversion = true` was read backwards. Declare active-low buttons with `inversion = true` on the `AGPIO`.

## Button

Wraps an `AGPIO` reference. Provides debounced state and edge detection. The state follows the logical level of the adapter (`GetState()`), so it is pressed when the adapter reports the button as pressed.

```cpp
Buttons::Button btn(gpioAdapter);

btn.Update(currentTime);        // Call every tick
btn.SetDebounceTime(30ms);      // Default: 50ms

btn.IsPressed();                // true while held
btn.WasPressed();               // rising edge (just pressed)
btn.WasReleased();              // falling edge (just released)
btn.HasChanged();               // state changed this tick
```

A new level is accepted only after it has stayed unchanged for the debounce time. With a debounce time of 0 ms, a change is reported on the tick it is seen.

## Click

Represents a single button or a combination of buttons that must all be pressed together. Tracks press duration and provides edge detection for the combination. A combination with no buttons is never pressed.

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

The press and release handlers are called by `Core` for a click that belongs to an enabled event (`AddEvent`), or for a click registered with `RegisterClick`. Clicks registered with `RegisterClick` need no event.

The template parameter `MaxButtons` (default 4) controls the maximum number of buttons in a combination:

```cpp
Buttons::Click<8> wideCombo({btn1, btn2, btn3, btn4, btn5});
```

`ClickDefault` is an alias for `Click<4>`.

Buttons can also be added one at a time. `AddButton` returns `false` when all `MaxButtons` slots are used, and leaves the click unchanged:

```cpp
Buttons::ClickDefault combo(btnA);
combo.AddButton(btnB);      // true
combo.AddButton(btnC);      // true
```

A constructor cannot return a value. If it is given more buttons than `MaxButtons`, the first `MaxButtons` are kept and `IsComplete()` returns `false`. `Core::RegisterClick` and `Core::AddEvent` refuse an incomplete click, so the overflow cannot go unnoticed.

`ClickDefault` is the only click type that `Event` and `Core` accept. A `Click<8>` cannot be used with `MakeClickAction` yet (see [Known limitation](#known-limitation)).

### Push-to-talk

Press starts an action and release stops it. Register the click directly with the core; the handlers are called after the debounce time on each edge.

```cpp
// --- wiki: push-to-talk ---
AGPIO voiceButton = { 37, true };          // GPIO37, active-low: pressed = pin low
Buttons::Button voice(voiceButton);
Buttons::ClickDefault voiceClick(voice);

voiceClick.SetOnPushHandle([]() {
    StartRecording();                      // button pressed (after the debounce time)
});
voiceClick.SetOnPullHandle([]() {
    StopRecording();                       // button released (after the debounce time)
});

Buttons::Core<8> core;
core.RegisterClick(voiceClick);            // no event needed
// --- end ---
```

Call `core.ProcessEvents(...)` from the main loop as in the Quick Start. To stop the handlers, call `core.RemoveClick(voiceClick)` before `voiceClick` goes out of scope.

## Action

Defines the expected click type and repetition count. The count must be at least 1: `SetClicksCount(0)` returns `false` and leaves the count unchanged, and an `Action` constructed with 0 is invalid (`IsValid()` is `false`). `Event` and `Core::AddEvent` refuse an invalid action.

### Click Types

| Type          | Condition                                          |
|---------------|---------------------------------------------------|
| `Short`       | Duration < `shortClickThreshold` (default 500ms)  |
| `Long`        | Duration >= `shortClickThreshold` and < `longClickThreshold` |
| `VeryLong`    | Duration >= `longClickThreshold` (default 2000ms) |
| `Infinitely`  | Any duration. Hold until released; completes on release |
| `Other`       | Any duration. Completes on release               |

### `Infinitely` ("hold until released")

`Infinitely` is for an action that lasts as long as the button is held. It is valid for any hold length. The action completes when the click is released, not while it is held, so `onDone` fires on release. The sequence does not time out while the click is held.

`Infinitely` and `Other` behave the same. Use `Infinitely` when the intent is "hold until released".

Current behaviour: no callback fires while the button is held. An action that fires while held (for example, a continuous action) is not available.

```cpp
// 1 short click
Buttons::Action shortPress(Buttons::Action::ClickType::Short, 1);

// 3 short clicks (e.g., triple-click)
Buttons::Action tripleShort(Buttons::Action::ClickType::Short, 3);

// 1 long press
Buttons::Action longPress(Buttons::Action::ClickType::Long, 1);

// 1 very long press
Buttons::Action veryLong(Buttons::Action::ClickType::VeryLong, 1);

// Hold until released (completes on release, any length)
Buttons::Action hold(Buttons::Action::ClickType::Infinitely, 1);

// Fluent API: SetClickType returns the action; SetClicksCount returns false for 0
Buttons::Action custom;
custom.SetClickType(Buttons::Action::ClickType::Long);
custom.SetClicksCount(2);       // true
```

## Event

An Event is a sequence of Click+Action pairs. When all pairs in the sequence are completed in order, the `onDone` callback fires. Events support both global and per-action hold conditions.

An action completes on the release of its click. A hold of any length is valid for `Infinitely` and `Other`.

Steps and hold conditions have fixed capacity. A step that does not fit is refused: `AddStep` returns `false`, and a constructor records it so that `IsComplete()` is `false` (`Core::AddEvent` then refuses the event). See [Capacity and Errors](#capacity-and-errors).

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
reset.WhileHolding(powerBtn);  // Power must be held throughout; false if all 4 global holds are used
```

`WhileHolding` accepts at most 4 global hold buttons. The list form is all-or-nothing: if the whole list does not fit, it returns `false` and adds none of its buttons.

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
event.SetTimeout(5000ms);                      // Max idle time between sequence steps
event.SetTimeThresholds(400ms, 1500ms);        // short/long boundary, long/verylong boundary
event.SetMultiClickGap(300ms);                 // Max gap between repeated clicks
```

`sequenceTimeout` is idle time: holding a click does not count towards it. A press that lasts longer than the timeout is still a valid `VeryLong` or `Infinitely` action.

The template parameter `MaxSequences` (default 8) controls the maximum number of steps per event. `EventDefault` is an alias for `Event<8>`.

## Core

Central manager that owns the update loop. Automatically collects all buttons from registered events and clicks, and handles debounce updates.

```cpp
Buttons::Core<16, 8> core;  // Template parameters = max events (default 16), max standalone clicks (default 8)
```

### Timing configuration

All timing fields are public and use `std::chrono::milliseconds`:

| Field                  | Default  | Description                              |
|------------------------|----------|------------------------------------------|
| `debounceTime`         | 50ms     | Contact bounce filter duration           |
| `shortClickThreshold`  | 500ms    | Clicks shorter than this are "short"     |
| `longClickThreshold`   | 2000ms   | Clicks longer than this are "very long"  |
| `sequenceTimeout`      | 5000ms   | Max idle time between sequence actions   |
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

### Event and click management

```cpp
core.AddEvent(event);           // Register event; false if refused (see Capacity and Errors)
core.RemoveEvent(event);        // Unregister event
core.RegisterClick(click);      // Register a click without an event; false if refused (see Capacity and Errors)
core.RemoveClick(click);        // Unregister a standalone click
core.EnableAll();               // Enable all registered events (standalone clicks are not affected)
core.DisableAll();              // Disable all registered events
core.GetEventCount();           // Number of registered events
core.GetMaxEvents();            // Template capacity
core.GetEvent(index);           // Access event by index (nullptr if out of range)
core.RegisterButton(button);    // Manually register a button for the update loop
```

Disabling an event stops updating its clicks. Avoid disabling an event while a button in its click is held; the click may then report a press that started before the disable.

### Processing

Call `ProcessEvents` periodically with the time elapsed since the previous call. It returns `false` only if a click could not be recorded for the tick. With the template limits this cannot happen, and the result can be ignored:

```cpp
// In main loop
while (true) {
    core.ProcessEvents(measuredElapsed);   // measured, not the nominal period
    osDelay(10);
}

// Or in an RTOS task
void ButtonTask(void*) {
    uint64 lastMs = System::GetMs();
    for (;;) {
        uint64 nowMs = System::GetMs();
        core.ProcessEvents(std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(nowMs - lastMs)));
        lastMs = nowMs;
        osDelay(5);
    }
}
```

`deltaTime` must not be negative. The core adds the deltas to its own time base, so there is no drift inside the core; the accuracy of every timing is the accuracy of the elapsed time you pass in.

## Helper Functions

Two free functions simplify the creation of `ClickAction` structs:

```cpp
// Basic click-action pair
auto ca = Buttons::MakeClickAction(click, action);

// Click-action pair with per-action hold conditions. If the holds do not all fit,
// the returned action is not complete: Core::AddEvent refuses it.
auto ca = Buttons::MakeClickActionWithHold(click, action, {holdBtn1, holdBtn2});
```

## Capacity and Errors

Every operation with a fixed capacity returns `false` when the item does not fit, and leaves the object unchanged. Nothing is ignored silently.

| Call | Returns `false` when | Object left unchanged |
|------|----------------------|------------------------|
| `Click::AddButton(Button&)` | all `MaxButtons` slots are used | yes |
| `Event::AddStep(ClickAction&)` | all `MaxSequences` steps are used, or the step is not complete | yes |
| `Event::WhileHolding(Button&)` | all 4 global hold slots are used | yes |
| `Event::WhileHolding({...})` | the list does not fit in the remaining slots | yes (none added) |
| `ClickAction::WhileHolding(Button&)` | all 4 per-action hold slots are used | yes |
| `ClickAction::WhileHolding({...})` | the list does not fit in the remaining slots | yes (none added) |
| `Action::SetClicksCount(0)` | count is 0 | yes |
| `Core::AddEvent(event)` | event list full, event already registered, event not complete, or a button it uses does not fit in the 32-button list | yes |
| `Core::RegisterClick(click)` | click list full, click already registered, click not complete, or a button does not fit | yes |
| `Core::RegisterButton(button)` | 32-button list full | yes |
| `Core::RemoveEvent` / `RemoveClick` | not registered | yes |

Constructors cannot return a value, so an overflow in a constructor is recorded instead:

- `Click({...})` keeps the first `MaxButtons` buttons. `IsComplete()` is `false` when any were dropped.
- `Event({...})` keeps the first `MaxSequences` steps. `IsComplete()` is `false` when any were dropped, or a step is not complete.
- `MakeClickActionWithHold({...})` adds none of the hold buttons when they do not all fit in 4 slots. The returned action's `IsComplete()` is `false` in that case.
- `Action(type, 0)` is invalid: `IsValid()` is `false`.

`Core::AddEvent` and `Core::RegisterClick` refuse any of these, so an incomplete object cannot reach the update loop.

`Core::ProcessEvents` returns `false` only if a click could not be recorded for the tick. With the template limits this cannot happen (the bound is the largest number of clicks that can be registered), and the result can be ignored.

### Known limitation

`MakeClickAction` and `MakeClickActionWithHold` take `ClickDefault&` (`Click<4>`). A `Click<8>` is not accepted, because `Event` stores its clicks as `Click<4>`. Supporting a wider click needs a change to `Event`'s storage, not a one-line change.

## Migrating from Earlier Versions

| Change | Action needed |
|--------|---------------|
| Active-low buttons are read through `AGPIO` inversion | Declare active-low buttons with `inversion = true` on the `AGPIO`. A button declared with `inversion = false` on active-low wiring is now read as released while pressed. |
| `AddEvent` rejects an event that is already registered | Do not register the same event twice. |
| Sequence timeouts are idle time only | A long `VeryLong` or `Infinitely` hold no longer fails the event while held. |
| `Core` has a second template parameter | `Core<N>` still works (`MaxClicks` defaults to 8). |
| `RegisterClick` / `RemoveClick` | Standalone push/pull clicks no longer need an event. |
| Capacity overflows return `false` | Check the result of `AddEvent`, `RegisterClick`, `RegisterButton`, `WhileHolding`, `AddButton` and `AddStep`. Silent truncation no longer happens. |
| `SetClicksCount` returns `bool` (was `Action&`) | Do not chain it: call `SetClicksCount` last, or call it as a statement. |
| `WhileHolding` returns `bool` (was `void`) | No change for statement use. |
| `ProcessEvents` returns `bool` (was `void`) | No change for statement use. |

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
| `IsPressed() -> bool` | All buttons held (false for an empty combination) |
| `WasJustPressed() -> bool` | Rising edge |
| `WasJustReleased() -> bool` | Falling edge |
| `GetPressDuration() -> ms` | Current/last press duration |
| `AddButton(Button&) -> bool` | Add a button; false and unchanged when all `MaxButtons` slots are used |
| `IsComplete() -> bool` | False when a constructor dropped buttons |
| `ForEachButton(Func)` | Iterate over contained buttons |

### `Action`

| Method | Description |
|--------|-------------|
| `Action()` | Default: Short, 1 click |
| `Action(ClickType, uint8 count)` | Construct with type and count; count 0 is invalid (`IsValid()` false) |
| `SetClickType(ClickType) -> Action&` | Set click type (fluent) |
| `SetClicksCount(uint8) -> bool` | Set repetition count; false and unchanged for 0 |
| `GetClickType() -> ClickType` | Get click type |
| `GetClicksCount() -> uint8` | Get repetition count |
| `IsValid() -> bool` | Count is at least 1 |
| `IsValidDuration(ms, ms short, ms long) -> bool` | Check if duration matches click type |

### `Event<MaxSequences>`

| Method | Description |
|--------|-------------|
| `Event(initializer_list<ClickAction<>>)` | Construct with sequence of click-actions |
| `Event(ClickAction<>&)` | Single click-action |
| `Enable()` | Enable and reset |
| `Disable()` | Disable processing |
| `Reset()` | Reset state to Idle |
| `AddStep(const ClickAction<>&) -> bool` | Append a step; false and unchanged when full or the step is not complete |
| `IsComplete() -> bool` | False when a constructor dropped steps or an invalid step |
| `WhileHolding(Button&) -> bool` | Add global hold condition; false when 4 are used |
| `WhileHolding(initializer_list<ref<Button>>) -> bool` | Add several global holds; all or nothing |
| `SetOnDoneHandle(function<void()>)` | Callback on sequence completion |
| `SetOnFailedHandle(function<void()>)` | Callback on sequence failure |
| `SetTimeout(ms)` | Max idle time between sequence steps |
| `SetTimeThresholds(ms short, ms long)` | Click type boundaries |
| `SetMultiClickGap(ms)` | Max gap for multi-clicks |
| `GetState() -> State` | Current state |
| `IsEnabled() -> bool` | Whether enabled |
| `ForEachButton(Func)` | Iterate all buttons in event |

### `Core<MaxEvents, MaxClicks>`

| Method | Description |
|--------|-------------|
| `AddEvent(EventDefault&) -> bool` | Register event; false if refused (see Capacity and Errors) |
| `RemoveEvent(EventDefault&) -> bool` | Unregister event |
| `RegisterClick(ClickDefault&) -> bool` | Register a standalone click; false if refused (see Capacity and Errors) |
| `RemoveClick(ClickDefault&) -> bool` | Unregister a standalone click |
| `EnableAll()` | Enable all events |
| `DisableAll()` | Disable all events |
| `ProcessEvents(ms deltaTime) -> bool` | Main update loop; deltaTime is the measured elapsed time, not negative; false only if a click could not be recorded (not reachable with the template limits) |
| `UpdateTimeSettings()` | Propagate timing to buttons/events |
| `GetEventCount() -> size_t` | Number of registered events |
| `GetMaxEvents() -> size_t` | Template capacity |
| `GetEvent(size_t) -> EventDefault*` | Access by index |
| `RegisterButton(Button&) -> bool` | Manual button registration; false when the 32-button list is full |
