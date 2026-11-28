# Angles

Helpers for angles and signs, in `Utilities/Math/Simulation/Angles.h`.

| Function | Value |
|---|---|
| `TwoPi<T>()` | 2 pi |
| `Pi<T>()` | pi |
| `WrapAngle(angle)` | the angle in `[0, 2 pi)` with the same value modulo 2 pi |
| `Sign(value)` | -1, 0 or 1; the sign of zero is zero |
| `StandardGravity<T>()` | 9.80665 m/s^2, the conventional standard gravity (1901 General Conference on Weights and Measures) |

```cpp
#include <Utilities/Math/Simulation/Angles.h>

void Run() {
	const double value = Simulation::WrapAngle(-0.5);
	const double direction = Simulation::Sign(-3.0);
	const double turn = Simulation::TwoPi<double>();
	const double half = Simulation::Pi<double>();
	const double gravity = Simulation::StandardGravity<double>();
}
```
