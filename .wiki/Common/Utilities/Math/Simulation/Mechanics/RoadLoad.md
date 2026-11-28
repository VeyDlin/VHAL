# RoadLoad

A vehicle on a road, as a load on the rotor. The block has no state. The road force is the road-load equation (Gillespie,
*Fundamentals of Vehicle Dynamics*, SAE, 1992), with the rolling term taken with the sign of the speed, so that it opposes the
motion and is zero at standstill:

```
F = 1/2 rho CdA v |v| + Crr m g cos(theta) sign(v) + m g sin(theta)
```

The wheel speed is `v = wm r / G`. The load torque at the motor is `F r / G`, and the reflected inertia is `m r^2 / G^2`. The grade
`theta` is an angle in radians. The vehicle values belong to the caller: the library holds no vehicle. The block needs a rotor in the
chain; its outputs are read by the [Rotor](Rotor.md), which adds the reflected inertia to its own.

## Setters

```cpp
#include <Utilities/Math/Simulation/Mechanics/RoadLoad.h>

void Run() {
	Simulation::RoadLoad<double> road;
	road.SetMass(1000.0);
	road.SetWheelRadius(0.3);
	road.SetGearRatio(10.0);
	road.SetRollingResistance(0.01);
	road.SetDragArea(0.6);
	road.SetAirDensity(1.2);
	road.SetGrade(0.02);
}
```

`SetMass` is in kg, `SetWheelRadius` in m, `SetGearRatio` is the overall ratio from the motor to the wheel (positive),
`SetRollingResistance` is dimensionless, `SetDragArea` is the product `C_d A` in m^2, `SetAirDensity` in kg/m^3 (the value is the
caller's; about 1.2 to 1.3 at sea level), and `SetGrade` is an angle in radians, positive uphill. `SetGrade` can be set while the
plant runs.

## LoadTorque and ReflectedInertia

`LoadTorque()` (N m, at the motor) and `ReflectedInertia()` (kg m^2) at the end of the last period.

```cpp
Simulation::RoadLoad<double> road;
const double load = road.LoadTorque();
const double inertia = road.ReflectedInertia();
```
