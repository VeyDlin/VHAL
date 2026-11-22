# Animation — Tween & Spring Animation Library

Header-only animation library with two modes: **tween** (time-based interpolation with easing) and **spring** (physics-based spring simulation). Works with any `RealType` — `float`, `double`, or `IQ<Q>` fixed-point. Optionally integrates with RTOS for automatic timer-driven updates. Includes `RgbAnimation` for color transitions.

## Quick Start

```cpp
#include <Utilities/Animation/Animation.h>

Animation<float> anim;

// Callback — called on every value update
anim.onUpdateValue = [](float val) {
    SetBrightness(val);
};

// Tween from current value to 1.0 over 500ms
anim.SetEasing(Easing<float>::Curve::EaseInOut);
anim.Tween(1.0f, std::chrono::milliseconds(500));

// In your main loop or timer interrupt:
anim.Update(GetCurrentTimeMs());
```

### IQ Fixed-Point (no FPU required)

```cpp
using iq = IQ<16>;

Animation<iq> anim;
anim.SetEasing(Easing<iq>::Curve::EaseInOut);
anim.Tween(iq(1), 500ms);

// All internal math is integer — no float operations
```

## Animation Modes

### Tween

Time-based interpolation from start to end value with configurable easing and duration.

```cpp
Animation<float> anim;
anim.SetEasing(Easing<float>::Curve::EaseOut);

// Tween from current value to target over duration
anim.Tween(1.0f, 300ms);

// Tween from explicit start to end
anim.Tween(0.0f, 1.0f, 500ms);

// Tween at constant speed (units/second)
anim.TweenSpeed(1.0f, 2.0f);         // to=1.0, speed=2.0 units/s
anim.TweenSpeed(0.0f, 1.0f, 3.0f);   // from=0, to=1.0, speed=3.0 units/s
```

A speed that is not positive gives a duration of 0: the tween ends at its target on the next `Update()`.

### Spring

Physics-based spring simulation. The value oscillates toward the target and settles naturally. Good for responsive UI and smooth servo control.

```cpp
Animation<float> anim;

// Configure spring parameters
anim.SetSpring(200.0f, 20.0f);    // stiffness, damping
anim.SetSpringEpsilon(0.001f);    // convergence threshold

// Set target — spring starts automatically
anim.SetTarget(1.0f);

// Change target mid-animation — spring redirects smoothly
anim.SetTarget(0.5f);
```

**Tuning guide:**
| Stiffness | Damping | Behavior |
|-----------|---------|----------|
| High (500) | Low (10) | Fast, bouncy |
| High (500) | High (40) | Fast, minimal overshoot |
| Low (50) | Low (5) | Slow, gentle bounce |
| Low (50) | High (15) | Slow, smooth settle |

## Easing Functions

`template <RealType Type = float>`

Four built-in easing types, or provide a custom function:

```cpp
// Built-in
anim.SetEasing(Easing<float>::Curve::Linear);     // constant speed
anim.SetEasing(Easing<float>::Curve::EaseIn);     // slow start (t²)
anim.SetEasing(Easing<float>::Curve::EaseOut);    // slow end
anim.SetEasing(Easing<float>::Curve::EaseInOut);  // slow start and end

// Custom easing function: T(T t), t ∈ [0, 1]
anim.SetEasing([](float t) {
    return t * t * t;  // cubic ease-in
});
```

With IQ:

```cpp
Animation<iq> anim;
anim.SetEasing(Easing<iq>::Curve::EaseInOut);

// Custom easing — also in IQ, no float
anim.SetEasing([](iq t) {
    return t * t * t;
});
```

## Callbacks

```cpp
Animation<float> anim;

// Called on every value update (tween step or spring tick)
anim.onUpdateValue = [](float val) {
    SetLedBrightness(val);
};

// Called when animation starts
anim.onStart = [](float val) {
    EnableOutput();
};

// Called when animation finishes (tween complete or spring settled)
anim.onStop = [](float val) {
    DisableOutput();
};
```

## Control

```cpp
anim.Pause();               // pause animation
anim.Resume();              // resume from paused state
anim.Stop();                // stop and fire onStop callback

bool active = anim.IsActive();
auto mode = anim.GetMode();          // Mode::None, Mode::Tween, Mode::Spring
float val = anim.GetValue();         // current value
float end = anim.GetEndValue();      // target value
anim.SetValue(0.5f);                 // set value without animation
```

## Update Loop

Call `Update(nowMs)` periodically with the current time in milliseconds:

```cpp
// In a timer interrupt or main loop:
void TimerISR() {
    anim.Update(System::GetMs());
}
```

## RTOS Integration

`AnimationRTOS` wraps `Animation` with an automatic RTOS timer thread — no manual `Update()` calls needed.

The internal timer thread is **started automatically** when an animation begins (`Tween`, `TweenSpeed`, `SetTarget`) and **stopped automatically** when the animation finishes or is explicitly stopped. When idle, the timer thread is suspended and consumes no CPU time.

```cpp
#include <Utilities/Animation/AnimationRTOS.h>

// Template parameters: value type, RTOS thread stack size
AnimationRTOS<float, 256> anim;

// Set update rate
anim.SetFps(60);                        // 60 FPS (16 ms interval, whole milliseconds); clamped to 1..1000 fps
anim.SetInterval(std::chrono::milliseconds(10));  // or explicit interval

// Use like regular Animation — timer starts/stops automatically
anim.onUpdateValue = [](float val) { SetBrightness(val); };
anim.Tween(1.0f, 500ms);

// Block until animation completes
anim.WaitEnd();
```

### Lifecycle

```
AnimationRTOS created → timer thread created (suspended)
    ↓
Tween()/TweenSpeed()/SetTarget() called → timer.Start() → thread resumes, calls Update() periodically
    ↓
Animation finishes or Stop() called → timer.Stop() → thread suspended again
```

## RgbAnimation

Animates `Colors::FRgb` color values (from `Utilities/Data/Colors/Colors.h`, three `float` channels `r`, `g`, `b`) using a single internal `Animation<T>` as a 0→1 progress driver. Lightweight — one animation instance, not three. `T` is a `RealType`: `float`, `double` or `IQ<Q>` (`RgbAnimation<IQ<24>>` is host-tested; the blend converts the channels to `T` first). The colours themselves stay `float`. `T` also needs the spring range described under Type Support when the spring is used.

```cpp
#include <Utilities/Animation/RgbAnimation.h>

// Without RTOS — call Update() manually
RgbAnimation<float> anim;

anim.onUpdateValue = [](Colors::FRgb color) {
    SetLedColor(color.r, color.g, color.b);
};

anim.Tween({1, 0, 0}, {0, 0, 1}, 1000ms);  // red → blue

// In timer:
anim.Update(System::GetMs());
```

### RTOS version

Pass `AnimationRTOS` as the second template parameter:

```cpp
#include <Utilities/Animation/AnimationRTOS.h>
#include <Utilities/Animation/RgbAnimation.h>

RgbAnimation<float, AnimationRTOS<float, 128>> anim;

// Configure timer via GetProgress()
anim.GetProgress().SetInterval(10ms);

anim.onUpdateValue = [](Colors::FRgb color) {
    SetLedColor(color.r, color.g, color.b);
};

anim.SetEasing(Easing<float>::Curve::EaseInOut);
anim.Tween({0, 0, 0}, {1, 1, 1}, 2000ms);  // black → white
```

### Spring mode

```cpp
RgbAnimation<float> anim;
anim.SetSpring(100.0f, 10.0f);
anim.SetTarget({1, 0, 0});   // chase red
anim.SetTarget({0, 1, 0});   // retarget to green mid-animation
```

## Type Support

`Animation<T>` requires `T` to satisfy the `RealType` concept:
- `float`, `double` — standard floating point
- `IQ<Q>` — fixed-point math (no FPU required)

Integral types such as `int` do not satisfy `RealType`. Tween time is measured in integer milliseconds, and the fraction
elapsed / duration is computed from those integers before it becomes `T`: for `IQ<Q>` in 64-bit integer arithmetic, so a
long duration never saturates the type (`IQ<24>`, range 128, runs a 1000 ms tween). Durations must stay below 2^63 ms.
Float divides the two converted integers.

Ranges by type:
- Tweens (`Tween`, `TweenSpeed`) work with any `RealType` whose range holds the start, the end and the difference
  `end - start`, because the value is `from + (to - from) * t`. A difference beyond the range saturates silently: `IQ<24>`
  (range 128) tweened from -100 to 100 reads -36 at the midpoint instead of 0, while `IQ<20>` (range 2048) gives the correct
  value for -1000 to 1000. Host tests cover `float`, `IQ<16>`, `IQ<20>` and `IQ<24>` (a 0 to 100 tween).
- The spring (`SetSpring`, `SetTarget`) needs a type whose range holds the default stiffness of 200. The force is 200 times
  the distance, so the type must hold 200 times the largest distance from the target, and a force beyond the range saturates
  silently (about 10 for `IQ<20>`). That distance limit depends on run-time values and is not checked; keep spring distances
  within it, or use `float`. `float`, `double` and `IQ<Q>` with Q of 23 or less qualify; `IQ<24>` (range 128) does not compile
  in `SetSpring` or `SetTarget`, and the `static_assert` says so. The spring's time step is also an integer fraction of 1000 ms,
  so `IQ<22>` and `IQ<23>` (ranges below 1000) run at the right step. Spring host tests cover `float`, `IQ<16>`, `IQ<20>`,
  `IQ<22>` and `IQ<23>` (a 0 to 1 spring at 17 ms ticks).

Implicit conversions from `int` and `float` literals work transparently with IQ — no `T(...)` wrappers needed:

```cpp
Animation<iq> anim;
anim.SetSpring(200, 20);      // int → iq implicitly
anim.SetSpringEpsilon(0.001f); // float → iq implicitly
anim.Tween(0, 1, 500ms);      // just works
```
