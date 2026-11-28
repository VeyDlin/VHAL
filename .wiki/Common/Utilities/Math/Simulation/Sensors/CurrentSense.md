# CurrentSense

Phase-current sensing with an offset and a gain error on each phase: the measured current is `(1 + g) i + o`. The true phase
currents come from the dq currents and the electrical angle through the library's inverse Park and inverse Clarke transforms
(see the FOC transforms, which this block reuses; their range is set by the number type). A balanced set sums to zero. The block
has no state and needs a motor in the chain.

## Offsets and gain

`SetOffsetA`, `SetOffsetB` and `SetOffsetC` set the offset of each phase sensor (A). `SetGainError` sets the gain error, shared by
the three phases (dimensionless).

```cpp
#include <Utilities/Math/Simulation/Sensors/CurrentSense.h>

void Run() {
	Simulation::CurrentSense<double> sense;
	sense.SetOffsetA(0.002);
	sense.SetOffsetB(-0.001);
	sense.SetOffsetC(0.0);
	sense.SetGainError(0.005);
}
```

## PhaseA, PhaseB and PhaseC

The measured phase currents (A) at the end of the last period.

```cpp
Simulation::CurrentSense<double> sense;
const double a = sense.PhaseA();
const double b = sense.PhaseB();
const double c = sense.PhaseC();
```

In a plant, a chain without this block has no phase current: `plant.CurrentSenseBlock()` refuses the call with
`no phase current: the chain has no CurrentSense block`.
