# FirstOrderThermal

A heater: a first-order thermal mass with a transport delay on its power input. The state is the rise of the temperature above the
ambient temperature, `theta`. The heating equation is the first-order model of Astrom and Murray, *Feedback Systems*, with the input
power delayed by a [DelayLine](DelayLine.md). The delay is a whole number of control periods.

```
C d(theta)/dt = P(t - td) - theta / Rth
T = Tamb + theta
```

For a power `P` applied at `t = 0` with a delay `td`, the step from ambient is `T(t) = Tamb + P Rth (1 - exp(-(t - td) / (Rth C)))`
for `t >= td`.

The type is `FirstOrderThermal<T, Depth>`. `Depth` is the number of control periods the delay line can hold, and it is the template
argument, so the delay storage is `Depth` values and no more. A plant takes the heater with `AddThermal<Depth>()`, and at most one heater
per chain.

## Setters

```cpp
#include <Utilities/Math/Simulation/Thermal/FirstOrderThermal.h>

void Run() {
	Simulation::FirstOrderThermal<double, 5> block;
	block.SetAmbient(25.0);
	block.SetResistance(5.0);
	block.SetCapacitance(10.0);
	block.SetDelaySteps(5);
	block.SetPower(10.0);
}
```

`SetAmbient` is the ambient temperature, in any unit (the output has the same unit). `SetResistance` (K/W) and `SetCapacitance` (J/K)
must be positive: the plant checks them when the chain is complete (`Simulation: the thermal resistance must be positive`). `SetDelaySteps`
is the delay in control periods, from 1 to `Depth`; the builder refuses zero (`Simulation: depth must be positive`) and a delay longer
than the line (`Simulation: the delay is longer than the line`). `SetPower` is the heater power in W and can be set while the plant runs;
the value of one period reaches the mass after the delay.

In a plant the setters are the builder's: `Ambient`, `ThermalResistance`, `ThermalCapacitance`, `DelaySteps` and `Power`, and the
heater is read with `Thermal()`, as in [Plant](../Plant.md).

## Temperature

`Temperature()` is the temperature (the ambient plus the rise) at the end of the last period.

```cpp
Simulation::FirstOrderThermal<double, 5> block;
const double temperature = block.Temperature();
const std::size_t capacity = Simulation::FirstOrderThermal<double, 5>::maxDelaySteps;
```

`maxDelaySteps` is the depth, which is the largest delay the setter accepts.
