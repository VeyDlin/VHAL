# Loads

The load torques of a rotor. Each is stateless and adds its torque to the bus. The sum of the loads enters the rotor's equation as
`tau_load`. Each load is one block, so a chain takes only the loads it uses.

## ConstantLoad

A constant torque in N m that the load applies against the rotation. A positive value opposes forward rotation, so a motor with
less torque than the load runs backwards; a negative value helps forward rotation.

```cpp
#include <Utilities/Math/Simulation/Mechanics/Loads.h>

void Run() {
	Simulation::ConstantLoad<double> constant;
	constant.SetTorque(0.01);
	const double torque = constant.Torque();
}
```

`SetTorque` can be set while the plant runs. In a builder the same value is `.Torque(...)` after `.Add<ConstantLoad>()`.

## QuadraticLoad

A load that grows with the square of the speed, such as a fan: `tau = k wm |wm|`, with `k` in N m s^2/rad^2.

```cpp
Simulation::QuadraticLoad<double> fan;
fan.SetCoefficient(1.0e-6);
const double coefficient = fan.Coefficient();
```

In a chain with a [TwoMassShaft](TwoMassShaft.md) the quadratic load is not read.
