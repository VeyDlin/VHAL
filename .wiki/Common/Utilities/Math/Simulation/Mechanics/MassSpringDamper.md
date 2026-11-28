# MassSpringDamper

A mass on a spring with a damper, driven by a force that the caller sets while the plant runs:

```
m x'' + c x' + k x = F(t)
```

The states are the position and the velocity. The block is a standalone chain: it does not take part in a motor chain and needs no
other block. It is used by the filter and observer tests as a second mechanical plant.

## Parameters

`Parameters` is an aggregate of `mass` (kg, positive), `stiffness` (N/m) and `damping` (N s/m).

```cpp
#include <Utilities/Math/Simulation/Mechanics/MassSpringDamper.h>

void Run() {
	Simulation::MassSpringDamper<double>::Parameters parameters;
	parameters.mass = 1.0;
	parameters.stiffness = 100.0;
	parameters.damping = 2.0;

	Simulation::MassSpringDamper<double> oscillator(parameters);
}
```

## SetForce

`SetForce(F)` sets the external force in N. It can be set while the plant runs.

```cpp
Simulation::MassSpringDamper<double> oscillator(parameters);
oscillator.SetForce(0.0);
```

## SetInitialPosition and SetInitialVelocity

These set the states a plant starts from (m and m/s).

```cpp
Simulation::MassSpringDamper<double> oscillator(parameters);
oscillator.SetInitialPosition(1.0);
oscillator.SetInitialVelocity(0.0);
```

## Position and Velocity

`Position()` (m) and `Velocity()` (m/s) at the end of the last period.

```cpp
Simulation::MassSpringDamper<double> oscillator(parameters);
const double position = oscillator.Position();
const double velocity = oscillator.Velocity();
```

The free decay from `x0 = 1` at rest is `x(t) = exp(-s t) (x0 cos(wd t) + (s x0 / wd) sin(wd t))`, with `s = c / (2 m)` and
`wd = sqrt(k / m - s^2)`. The self-check compares it at one second.
