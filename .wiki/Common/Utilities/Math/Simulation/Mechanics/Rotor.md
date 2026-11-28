# Rotor

A rigid rotor with viscous and Coulomb friction. The states are the speed and the angle of the shaft:

```
J dwm/dt = Te - tau_load - b wm - tc sign(wm)
d(thetaM)/dt = wm
```

`J` is the rotor inertia plus the reflected inertia of a road load when one is in the chain. The loads of the chain add to
`tau_load`. The Coulomb term is zero at standstill, so the model has no stiction. The rotor needs a motor in the chain, which gives
the torque `Te`.

## SetInertia

`SetInertia(J)` sets the inertia, kg m^2. The speed is divided by it, so it must be positive: the builder's `Inertia` refuses zero, and
the plant checks the inertia when the chain is complete (`Simulation: the rotor inertia must be positive`).

```cpp
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>

void Run() {
	Simulation::Rotor<double> rotor;
	rotor.SetInertia(1.0e-4);
}
```

## SetViscous and SetCoulomb

`SetViscous(b)` is the viscous coefficient (N m s) and `SetCoulomb(tc)` the Coulomb torque (N m).

```cpp
Simulation::Rotor<double> rotor;
rotor.SetViscous(1.0e-6);
rotor.SetCoulomb(1.0e-5);
```

## SetInitialSpeed and SetInitialAngle

These set the states a plant starts from, rad/s and rad.

```cpp
Simulation::Rotor<double> rotor;
rotor.SetInitialSpeed(10.0);
rotor.SetInitialAngle(0.5);
```

## Speed and ThetaM

`Speed()` (rad/s) and `ThetaM()` (rad, not wrapped) at the end of the last period.

```cpp
Simulation::Rotor<double> rotor;
const double speed = rotor.Speed();
const double angle = rotor.ThetaM();
```
