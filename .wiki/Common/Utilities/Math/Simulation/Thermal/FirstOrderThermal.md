# FirstOrderThermal

A heater: a first-order thermal mass with a transport delay on its power input. The state is the rise of the temperature above the
ambient temperature, `theta`. The heating equation is the first-order model of Astrom and Murray, *Feedback Systems*, with the input
power delayed by a [DelayLine](DelayLine.md). The delay is a whole number of control periods.

```
C d(theta)/dt = P(t - td) - theta / Rth
T = Tamb + theta
```

For a power `P` applied at `t = 0` with a delay `td`, the step from ambient is `T(t) = Tamb + P Rth (1 - exp(-(t - td) / (Rth C)))`
for `t >= td`. The block is a standalone chain.

## Setters

```cpp
#include <Utilities/Math/Simulation/Thermal/FirstOrderThermal.h>

void Run() {
	Simulation::FirstOrderThermal<double> block;
	block.SetAmbient(25.0);
	block.SetResistance(5.0);
	block.SetCapacitance(10.0);
	block.SetDelaySteps(5);
	block.SetPower(10.0);
}
```

`SetAmbient` is the ambient temperature, in any unit (the output has the same unit). `SetResistance` (K/W) and `SetCapacitance` (J/K)
must be positive. `SetDelaySteps` is the delay in control periods, from 1 to `maxDelaySteps` (256); the builder refuses zero
(`depth must be positive`). `SetPower` is the heater power in W and can be set while the plant runs; the value of one period
reaches the mass after the delay.

## Temperature

`Temperature()` is the temperature (the ambient plus the rise) at the end of the last period.

```cpp
Simulation::FirstOrderThermal<double> block;
const double temperature = block.Temperature();
const std::size_t capacity = Simulation::FirstOrderThermal<double>::maxDelaySteps;
```
