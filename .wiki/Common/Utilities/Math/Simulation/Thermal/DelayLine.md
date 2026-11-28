# DelayLine

A transport delay on the control step. `Push` stores the value of one control period, and `Delayed(depth)` returns the value pushed
`depth - 1` pushes before the last one, so `Delayed(1)` is the last value pushed. The buffer is a fixed array of `Capacity` values, so
no memory is allocated. The depth is from 1 to `Capacity`. Before a value has been pushed that far back, the value is zero.

The line is separate from the library's `RingBuffer` (`Utilities/Buffer/RingBuffer.h`). A ring buffer is a queue: its values are read
in order and consumed, with uncommitted reads, and a read at an offset behind the last push is not part of it. The delay needs that
read on every period, and it needs no policy and no 16-bit size limit, so it is a small class of its own.

The [FirstOrderThermal](FirstOrderThermal.md) block holds one of these, with the `Depth` of the block as its capacity.

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
