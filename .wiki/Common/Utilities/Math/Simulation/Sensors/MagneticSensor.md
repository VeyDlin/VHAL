# MagneticSensor

An absolute magnetic angle sensor: the electrical angle of the motor with a Gaussian error. The standard deviation is given in
degrees. One sample is drawn at the end of each control period, so every RK4 step of a period sees the same error. The first
period has no error, because the first sample is drawn at the end of it. The block needs a motor in the chain.

## SetNoiseDeg

`SetNoiseDeg(sigma)` sets the standard deviation of the error, in degrees.

```cpp
#include <Utilities/Math/Simulation/Sensors/MagneticSensor.h>

void Run() {
	Simulation::MagneticSensor<double> sensor;
	sensor.SetNoiseDeg(0.05);
}
```

## SetSeed

`SetSeed(seed)` sets the seed of the error sequence. It must not be zero: the builder's `Seed` refuses zero at compile time
(`Simulation: the seed must not be zero`).

```cpp
Simulation::MagneticSensor<double> sensor;
sensor.SetSeed(7);
```

## Angle

`Angle()` returns the measured electrical angle (rad, in `[0, 2 pi)`) at the end of the last period.

```cpp
Simulation::MagneticSensor<double> sensor;
const double angle = sensor.Angle();
```
