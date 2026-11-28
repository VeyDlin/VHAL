# Pmsm

Permanent-magnet synchronous motor in the amplitude-invariant dq frame. The states are the d and q currents and the electrical
angle. A surface machine has `Ld == Lq`. The equations follow the dq model of a permanent-magnet machine (Krause, Wasynczuk,
Sudhoff and Pekarek, *Analysis of Electric Machinery and Drive Systems*), written in the same form as the Pulsim documentation
(https://lgili.github.io/Pulsim/0.10.0/motor-models/):

```
vd = Rs id + Ld d(id)/dt - we Lq iq
vq = Rs iq + Lq d(iq)/dt + we (Ld id + psi)
Te = 1.5 p (psi iq + (Ld - Lq) id iq)
we = p wm,   d(thetaE)/dt = we
```

The flux linkage `psi` is the peak value per phase, in Wb. The speed `wm` comes from the bus (the rotor). The voltage is the
command of the block, or the voltage that an [Inverter](../Electrical/Inverter.md) applied to it.

## Parameters

`Parameters` is an aggregate of `resistance` (ohm), `ld` and `lq` (H), `psi` (Wb) and `polePairs`.

```cpp
#include <Utilities/Math/Simulation/Motors/Pmsm.h>

void Run() {
	Simulation::Pmsm<double>::Parameters parameters;
	parameters.resistance = 0.5;
	parameters.ld = 2.0e-3;
	parameters.lq = 3.0e-3;
	parameters.psi = 0.01;
	parameters.polePairs = 4;

	Simulation::Pmsm<double> motor(parameters);
}
```

## SetVoltageCommand

`SetVoltageCommand(vd, vq)` sets the voltage command in the dq frame, V. It can be set while the plant runs.

```cpp
Simulation::Pmsm<double> motor(parameters);
motor.SetVoltageCommand(1.0, 0.0);
```

## CurrentD, CurrentQ, ThetaE and Torque

These return the d and q currents (A), the electrical angle (rad, not wrapped) and the electromagnetic torque (N m) at the end of
the last period of a plant. Before the first period they are zero.

```cpp
Simulation::Pmsm<double> motor(parameters);
const double id = motor.CurrentD();
const double iq = motor.CurrentQ();
const double thetaE = motor.ThetaE();
const double torque = motor.Torque();
```

Read through a plant, as in [Plant](../Plant.md): `plant.Get<Simulation::Pmsm>().CurrentQ()`.

## Checks

The self-checks of this block are: the electrical time constant `L / R` at standstill, the steady current `V / Rs` at a held rotor,
the back-EMF with `vq = we psi`, the torque against the formula, the fourth-order convergence of the step, and the energy balance
(the input power equals the copper loss, the mechanical power and the rate of the stored energy `0.75 (Ld id^2 + Lq iq^2)`, to
1e-3 of the input energy).
