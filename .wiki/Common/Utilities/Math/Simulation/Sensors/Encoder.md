# Encoder

An incremental encoder. It counts the shaft angle in a number of counts per mechanical turn: the count is `floor(turns * Counts)`,
with the turns taken modulo one turn, so a count is always below `Counts`. The encoder is stateless and needs a rotor in the chain.

## SetCounts

`SetCounts(n)` sets the counts per mechanical turn. In a builder `.Counts(n)` refuses zero (`counts must be positive`).

```cpp
#include <Utilities/Math/Simulation/Sensors/Encoder.h>

void Run() {
	Simulation::Encoder<double> encoder;
	encoder.SetCounts(2048);
}
```

## Counts

`Counts()` returns the count at the end of the last period.

```cpp
Simulation::Encoder<double> encoder;
const std::uint32_t count = encoder.Counts();
```
