# DelayLine

A transport delay on the control step. `Push` stores the value of one control period, and `Delayed(depth)` returns the value that was
pushed `depth` periods before the last push. The buffer is a fixed array of `Capacity` values, so no memory is allocated. The depth
is from 1 to `Capacity`: a depth of one is the last value pushed. Before a value has been pushed that far back, the value is zero.

The [FirstOrderThermal](FirstOrderThermal.md) block holds one of these with 256 values.

## Push

```cpp
#include <Utilities/Math/Simulation/Thermal/DelayLine.h>

void Run() {
	Simulation::DelayLine<double, 8> line;
	line.Push(1.5);
}
```

## Delayed

```cpp
Simulation::DelayLine<double, 8> line;
line.Push(1.5);
const double last = line.Delayed(1);
```
