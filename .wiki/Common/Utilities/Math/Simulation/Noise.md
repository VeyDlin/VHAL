# Noise

`Simulation::Noise` is a seeded generator for the sensor errors of one plant. Each instance has its own state, so two plants in
one test do not share a sequence, and the sequence does not depend on the C library. The generator is xorshift32 (Marsaglia,
Journal of Statistical Software, 2003, the triple 13, 17, 5). The Gaussian is the Box-Muller transform (Box and Muller, 1958),
using both uniform values for one sample.

## Noise

`Noise(seed)` starts the sequence. The seed must not be zero: a zero state stays zero.

```cpp
#include <Utilities/Math/Simulation/Noise.h>

void Run() {
	Simulation::Noise noise(1234);
}
```

## Uniform

`Uniform()` returns a value in (0, 1).

```cpp
Simulation::Noise noise(1234);
const double uniform = noise.Uniform();
```

## Gaussian

`Gaussian(sigma)` returns a value with mean zero and standard deviation `sigma`.

```cpp
Simulation::Noise noise(1234);
const double gaussian = noise.Gaussian(0.5);
```
