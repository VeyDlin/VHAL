# TwoMassShaft

An elastic shaft between a motor inertia and a load inertia, with a spring and a damper in the shaft. It replaces the
[Rotor](Rotor.md) in a chain. The equations are the two-inertia model (Krause, Wasynczuk, Sudhoff and Pekarek, *Analysis of Electric
Machinery and Drive Systems*, the mechanical model of a drive with a flexible shaft):

```
Jm d(wm)/dt = Te - k (thm - thl) - c (wm - wl) - bm wm
Jl d(wl)/dt = k (thm - thl) + c (wm - wl) - tau_load - bl wl
d(thm)/dt = wm,  d(thl)/dt = wl
```

The load side takes the constant, quadratic and road loads of the chain, and the reflected inertia of a road load; each of them reads the
load-side speed. The Coulomb friction of [Rotor](Rotor.md) is not in this model. The resonance of
the shaft with no damping is `sqrt(k (Jm + Jl) / (Jm Jl))`.

## Setters

```cpp
#include <Utilities/Math/Simulation/Mechanics/TwoMassShaft.h>

void Run() {
	Simulation::TwoMassShaft<double> shaft;
	shaft.SetMotorInertia(1.0e-4);
	shaft.SetLoadInertia(4.0e-4);
	shaft.SetStiffness(100.0);
	shaft.SetDamping(0.01);
	shaft.SetMotorViscous(1.0e-6);
	shaft.SetLoadViscous(1.0e-6);
	shaft.SetInitialMotorAngle(0.1);
}
```

`SetMotorInertia` and `SetLoadInertia` take kg m^2 and must be positive. `SetStiffness` is in N m/rad, `SetDamping` in N m s/rad,
and the viscous coefficients in N m s. `SetInitialMotorAngle` starts the shaft with that twist, and the load at zero.

## Outputs

`Speed()` and `ThetaM()` are the motor side (rad/s and rad). `LoadSpeed()` and `LoadThetaM()` are the load side, at the end of the
last period.

```cpp
Simulation::TwoMassShaft<double> shaft;
const double speed = shaft.Speed();
const double angle = shaft.ThetaM();
const double loadSpeed = shaft.LoadSpeed();
const double loadAngle = shaft.LoadThetaM();
```
