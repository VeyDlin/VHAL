# BrushedDc

Brushed DC motor, electrical part. The state is the armature current. The equations are the armature circuit and the torque of a
separately excited dc machine (Krause, Wasynczuk, Sudhoff and Pekarek, *Analysis of Electric Machinery and Drive Systems*):

```
V = R i + L di/dt + Ke w
torque = Kt i,  with Kt = Ke in SI units
```

The block has one torque constant, which is both `Kt` and `Ke` in SI units. The mechanical part is a [Rotor](../Mechanics/Rotor.md)
on the same chain.

## Parameters

`Parameters` is an aggregate of `resistance` (ohm), `inductance` (H) and `torqueConstant` (N m/A, which is also V s/rad).

```cpp
#include <Utilities/Math/Simulation/Motors/BrushedDc.h>

void Run() {
	Simulation::BrushedDc<double>::Parameters parameters;
	parameters.resistance = 1.0;
	parameters.inductance = 1.0e-3;
	parameters.torqueConstant = 0.05;

	Simulation::BrushedDc<double> motor(parameters);
}
```

## SetVoltageCommand

`SetVoltageCommand(v)` sets the terminal voltage, V. It can be set while the plant runs.

```cpp
Simulation::BrushedDc<double> motor(parameters);
motor.SetVoltageCommand(6.0);
```

## Current and Torque

`Current()` is the armature current (A) and `Torque()` the torque (N m), both at the end of the last period.

```cpp
Simulation::BrushedDc<double> motor(parameters);
const double current = motor.Current();
const double torque = motor.Torque();
```
