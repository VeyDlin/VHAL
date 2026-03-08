# Animation — Tween & Spring Animation Library

Header-only animation library with two modes: **tween** (time-based interpolation with easing) and **spring** (physics-based spring simulation). Works with any arithmetic type or custom vector type. Optionally integrates with RTOS for automatic timer-driven updates.

## Quick Start

```cpp
#include <Utilities/Animation/Animation.h>

Animation<float> anim;

// Callback — called on every value update
anim.onUpdateValue = [](float val) {
    SetBrightness(val);
};

// Tween from current value to 1.0 over 500ms
anim.SetEasing(Easing<>::Curve::EaseInOut);
anim.Tween(1.0f, std::chrono::milliseconds(500));

// In your main loop or timer interrupt:
anim.Update(GetCurrentTimeMs());
```

## Animation Modes

### Tween

Time-based interpolation from start to end value with configurable easing and duration.

```cpp
Animation<float> anim;
anim.SetEasing(Easing<>::Curve::EaseOut);

// Tween from current value to target over duration
anim.Tween(1.0f, 300ms);

// Tween from explicit start to end
anim.Tween(0.0f, 1.0f, 500ms);

// Tween at constant speed (units/second)
anim.TweenSpeed(1.0f, 2.0f);         // to=1.0, speed=2.0 units/s
anim.TweenSpeed(0.0f, 1.0f, 3.0f);   // from=0, to=1.0, speed=3.0 units/s
```

### Spring

Physics-based spring simulation. The value oscillates toward the target and settles naturally. Good for responsive UI and smooth servo control.

```cpp
Animation<float> anim;

// Configure spring parameters
anim.SetSpring(
    200.0f,   // stiffness — higher = faster, more oscillation
    20.0f     // damping — higher = less oscillation, faster settle
);
anim.SetSpringEpsilon(0.001f);  // convergence threshold

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
anim.SetEasing(Easing<>::Curve::Linear);     // constant speed
anim.SetEasing(Easing<>::Curve::EaseIn);     // slow start (t²)
anim.SetEasing(Easing<>::Curve::EaseOut);    // slow end
anim.SetEasing(Easing<>::Curve::EaseInOut);  // slow start and end

// Custom easing function: float(float t), t ∈ [0, 1]
anim.SetEasing([](float t) {
    return t * t * t;  // cubic ease-in
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
anim.SetFps(60);                        // 60 FPS (~17ms interval)
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

## Custom Types

`Animation<T>` works with any type that supports:
- `T + T`, `T - T`, `T * float` (for lerp)
- `T.Magnitude()` → `float` (for spring scalar diff)
- `T.Normalized()` → `T` (for spring advance direction)

Arithmetic types (`float`, `int`, etc.) work out of the box.

```cpp
// Example: animate a 2D position
struct Vec2 {
    float x, y;
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    float Magnitude() const { return sqrtf(x*x + y*y); }
    Vec2 Normalized() const { float m = Magnitude(); return {x/m, y/m}; }
};

Animation<Vec2> anim;
anim.SetSpring(150.0f, 15.0f);
anim.SetTarget({100.0f, 200.0f});
```
