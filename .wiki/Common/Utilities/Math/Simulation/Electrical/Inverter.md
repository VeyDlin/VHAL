# Inverter

An average model of a voltage-source inverter, without switching and without dead time. It limits the voltage command of the
[Pmsm](../Motors/Pmsm.md) to the linear range of a space-vector modulator: in the amplitude-invariant dq frame the largest voltage
vector the inverter can make has magnitude `Vdc / sqrt(3)` (Holtz, *Pulsewidth modulation for electronic power conversion*, Proc.
IEEE, 1994). A command beyond the limit is scaled onto the limit circle, keeping its direction. The block publishes the applied
voltage to the motor. It needs a PMSM in the chain.

## SetDcBus

`SetDcBus(Vdc)` sets the DC bus voltage in V. It can be set while the plant runs.

```cpp
#include <Utilities/Math/Simulation/Electrical/Inverter.h>

void Run() {
	Simulation::Inverter<double> inverter;
	inverter.SetDcBus(14.8);
}
```

## AppliedD and AppliedQ

The applied voltage in the dq frame (V) at the end of the last period.

```cpp
Simulation::Inverter<double> inverter;
const double vd = inverter.AppliedD();
const double vq = inverter.AppliedQ();
```
