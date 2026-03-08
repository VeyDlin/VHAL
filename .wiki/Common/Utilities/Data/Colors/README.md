# Colors — RGB Color Types with Gamma Correction

RGB color types for LED control with per-channel gamma correction. Compatible with `Animation<T>` for smooth color transitions.

## FRgb — Float RGB (0.0–1.0)

```cpp
#include <Utilities/Data/Colors/Colors.h>
using namespace Colors;

FRgb color(0.5f, 0.0f, 1.0f);  // purple

// Arithmetic
FRgb mixed = FRgb::Red() * 0.5f + FRgb::Blue() * 0.5f;
color += FRgb(0.1f, 0.0f, 0.0f);
color *= 0.8f;

// Comparison
if (color == FRgb::Black()) { ... }

// Named colors
FRgb c = FRgb::White();   // also: Black, Red, Green, Blue, Yellow, Cyan, Magenta
```

## URgb — Byte RGB (0–255)

```cpp
URgb pixel(255, 128, 0);  // orange

// Same operators as FRgb
URgb dimmed = pixel * 0.5f;

// Named colors
URgb c = URgb::Red();  // {0xFF, 0x00, 0x00}
```

## FRgb ↔ URgb Conversion

```cpp
FRgb linear(1.0f, 0.5f, 0.0f);
URgb bytes(linear);              // → {255, 128, 0} (with rounding)

URgb raw(200, 100, 50);
FRgb normalized(raw);            // → {0.784, 0.392, 0.196}
```

## GammaProfile — Per-Channel Gamma Correction

LED brightness is not linear — human eye perceives it logarithmically. Different LED colors (R, G, B) also have different luminous efficacy, so each channel needs its own correction.

```cpp
// Per-channel gamma + brightness scaling
GammaProfile gamma = {
    .gammaR = 2.0f,   .gammaG = 2.5f,   .gammaB = 2.8f,
    .scaleR = 1.0f,   .scaleG = 0.7f,   .scaleB = 1.0f
};

// Apply: linear color → PWM value
FRgb color = FRgb::White() * 0.5f;   // 50% brightness in linear space
URgb pwm = gamma.Apply(color);        // corrected for LED perception
SetLedPWM(pwm.r, pwm.g, pwm.b);

// Inverse: PWM value → linear color
FRgb linear = gamma.Inverse(pwm);

// Presets
auto g1 = GammaProfile::sRGB();     // standard sRGB (2.2 all channels)
auto g2 = GammaProfile::Linear();   // no correction (1.0)
```

### Tuning Per-Channel Gamma

| Parameter | Purpose | How to tune |
|-----------|---------|-------------|
| `gammaR/G/B` | Brightness curve per channel | Set each channel to max, sweep 0→1, adjust until perceived brightness is linear |
| `scaleR/G/B` | Max brightness balance | Set all channels to 1.0, adjust scales until white looks neutral (not greenish/bluish) |

Typical values for common RGB LEDs:
- Green is usually brightest → `scaleG` = 0.6–0.8
- Blue is usually dimmest → `gammaB` = 2.5–3.0
- Red is in between → defaults often work

## Animation + Gamma

`FRgb` and `URgb` are compatible with `Animation<T>` (support `+`, `-`, `*`, `Magnitude()`, `Normalized()`). Always animate in linear space, apply gamma only at the PWM output:

```cpp
Animation<FRgb> anim;
GammaProfile gamma = { ... };

anim.onUpdateValue = [&](FRgb color) {
    URgb pwm = gamma.Apply(color);
    SetLedPWM(pwm.r, pwm.g, pwm.b);
};

anim.Tween(FRgb::Black(), FRgb::Red(), 1000ms);
```
