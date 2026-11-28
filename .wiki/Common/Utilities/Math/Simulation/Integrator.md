# Integrator

`Simulation::Integrator<T, N>` is the classic fixed-step Runge-Kutta method of order four (Kutta 1901; Hairer, Norsett and Wanner,
*Solving Ordinary Differential Equations I*). The state is `N` values of `T`. The derivative is a callable that takes the state and
writes the derivative of every value. The callable is a template argument, so a step has no allocation and no indirection.

## Step

`Step(state, step, derivative)` advances `state` by one step of length `step`. The callable has the form
`void operator()(const T* state, T* derivative) const`.

```cpp
#include <Utilities/Math/Simulation/Integrator.h>
#include <array>

struct Decay {
	void operator()(const double* state, double* derivative) const {
		derivative[0] = -state[0];
	}
};

void Run() {
	std::array<double, 1> state{ 1.0 };
	Decay decay;
	Simulation::Integrator<double, 1>::Step(state, 0.1, decay);
}
```

The order of the method is four: halving the step divides the error by about 16. The stability limit on the real axis is
`|h lambda| <= 2.78`. A plant chooses its substeps so that the fastest time constant is resolved; see [Plant](Plant.md).
