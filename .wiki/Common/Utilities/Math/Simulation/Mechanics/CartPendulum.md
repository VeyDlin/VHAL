# CartPendulum

A pendulum on a cart, with the cart force as input. The angle is measured from the upright position, and the pole is a point mass
at the end of a massless rod of length `l`. The equations are the standard cart-pendulum equations (Astrom and Murray, *Feedback
Systems*, the cart-pendulum example), with `D = (M + m) - m cos^2(theta)`:

```
x'' = (F + m l theta'^2 sin(theta) - m g sin(theta) cos(theta)) / D
theta'' = ((M + m) g sin(theta) - cos(theta) (F + m l theta'^2 sin(theta))) / (l D)
```

The states are the cart position, its velocity, the pole angle and its rate. Near the upright position the linearisation with no
force has the eigenvalues 0 (twice) and `+/- sqrt((M + m) g / (M l))`.

## Parameters

`Parameters` is an aggregate of `cartMass` (M, kg), `poleMass` (m, kg) and `length` (l, m), all positive.

```cpp
#include <Utilities/Math/Simulation/Mechanics/CartPendulum.h>

void Run() {
	Simulation::CartPendulum<double>::Parameters parameters;
	parameters.cartMass = 1.0;
	parameters.poleMass = 0.1;
	parameters.length = 0.5;

	Simulation::CartPendulum<double> cart(parameters);
}
```

## SetForce

`SetForce(F)` sets the force on the cart in N. It can be set while the plant runs.

```cpp
Simulation::CartPendulum<double> cart(parameters);
cart.SetForce(0.0);
```

## SetInitialPosition and SetInitialAngle

`SetInitialPosition` sets the cart position (m) at the start. `SetInitialAngle` sets the pole angle from upright (rad) at the start.

```cpp
Simulation::CartPendulum<double> cart(parameters);
cart.SetInitialPosition(0.0);
cart.SetInitialAngle(1.0e-4);
```

## CartPosition and PoleAngle

`CartPosition()` (m) and `PoleAngle()` (rad, from upright) at the end of the last period.

```cpp
Simulation::CartPendulum<double> cart(parameters);
const double position = cart.CartPosition();
const double angle = cart.PoleAngle();
```

The self-checks are two. The first starts from a small angle at rest and compares the angle after 0.3 s with `theta0 cosh(lambda t)`,
with `lambda = sqrt((M + m) g / (M l))`, to 1e-3 relative. The second takes the Jacobian of the derivative at the upright position by
central differences and compares its entries with the linearisation: `A[0][1] = 1`, `A[1][2] = -m g / M`, `A[2][3] = 1` and
`A[3][2] = (M + m) g / (M l)`, which has the eigenvalues 0, 0 and `+/- lambda`.
