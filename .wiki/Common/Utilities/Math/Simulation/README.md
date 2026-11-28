# Simulation

Physical plant models for closed-loop host tests and PC programs. The models are double precision by default, integrated by a
fixed-step classic Runge-Kutta of order four with a substep count chosen by the caller. The module is header-only and sits under
`Utilities/Math`. It runs on the PC only, so it has no size table.

## Contents

- [Plant](Plant.md): the builder, the chain of blocks and the plant that integrates it
- [Integrator](Integrator.md) and [Angles](Angles.md): the RK4 step, the angle and sign helpers
- [Noise](Noise.md): the seeded generator of the sensor errors
- Motors: [Pmsm](Motors/Pmsm.md), [BrushedDc](Motors/BrushedDc.md)
- Mechanics: [Rotor](Mechanics/Rotor.md), [TwoMassShaft](Mechanics/TwoMassShaft.md), [RoadLoad](Mechanics/RoadLoad.md),
  [Loads](Mechanics/Loads.md), [MassSpringDamper](Mechanics/MassSpringDamper.md), [CartPendulum](Mechanics/CartPendulum.md)
- Electrical: [Inverter](Electrical/Inverter.md)
- Sensors: [Encoder](Sensors/Encoder.md), [MagneticSensor](Sensors/MagneticSensor.md), [CurrentSense](Sensors/CurrentSense.md)
- Thermal: [FirstOrderThermal](Thermal/FirstOrderThermal.md), [DelayLine](Thermal/DelayLine.md)

## Blocks

| Block | States | Stage | Reads from the bus | Page |
|---|---|---|---|---|
| `Pmsm` | 3: id, iq, thetaE | 0 | speed | [Pmsm](Motors/Pmsm.md) |
| `BrushedDc` | 1: current | 0 | speed | [BrushedDc](Motors/BrushedDc.md) |
| `Rotor` | 2: speed, thetaM | 0 | torque, loads, reflected inertia | [Rotor](Mechanics/Rotor.md) |
| `TwoMassShaft` | 4 | 0 | torque, constant, quadratic and road loads (load side) | [TwoMassShaft](Mechanics/TwoMassShaft.md) |
| `RoadLoad` | 0 | 1 | speed (load side with a TwoMassShaft) | [RoadLoad](Mechanics/RoadLoad.md) |
| `ConstantLoad`, `QuadraticLoad` | 0 | 1 | speed (quadratic; load side with a TwoMassShaft) | [Loads](Mechanics/Loads.md) |
| `Inverter` | 0 | 1 | voltage | [Inverter](Electrical/Inverter.md) |
| `Encoder` | 0 | 2 | thetaM | [Encoder](Sensors/Encoder.md) |
| `MagneticSensor` | 0 | 2 | thetaE (needs a PMSM) | [MagneticSensor](Sensors/MagneticSensor.md) |
| `CurrentSense` | 0 | 2 | currents, thetaE (needs a PMSM) | [CurrentSense](Sensors/CurrentSense.md) |
| `FirstOrderThermal` | 1: rise | 0 | none | [FirstOrderThermal](Thermal/FirstOrderThermal.md) |
| `MassSpringDamper` | 2 | 0 | none | [MassSpringDamper](Mechanics/MassSpringDamper.md) |
| `CartPendulum` | 4 | 0 | none | [CartPendulum](Mechanics/CartPendulum.md) |

The stage orders the publish pass, and it is listed in [Plant](Plant.md).

## Chain rules

The builder accepts its blocks in any order. The rules are checked on the finished chain, and each message names the block:

- one motor per chain, one rotor per chain (a `Rotor` or a `TwoMassShaft`), and one thermal block per chain;
- a rotor needs a motor, and a motor needs a rotor;
- a road load, a load, and an encoder need a rotor;
- an inverter needs a PMSM;
- a magnetic sensor and a current sense need a PMSM.

A setter exists only when its block is in the chain, and each accessor (`Road()`, `EncoderBlock()`, `CurrentSenseBlock()`, `Thermal()`)
exists only when its block is in the chain. A value that is refused at compile time stops the build with its message: a zero counts,
a zero seed, a zero delay, a non-positive parameter (an inertia, a mass, an inductance, a DC bus, a thermal resistance, and the others,
checked on the finished chain), and an `IQ` number type.

## Parameter sets

The library holds no motor parameter set. A parameter set is added only with its source, and any value derived from a source is given
with its formula in a comment. The examples on these pages use test values chosen for the checks; they are not motor parameters.

## Equations and sources

The equation of each block is in its page, with the source it follows:

- the dq model of the PMSM, the brushed motor, the two-inertia shaft, the rotor's equation of motion, and the amplitude-invariant
  transforms used by the current sense: Krause, Wasynczuk, Sudhoff and Pekarek, *Analysis of Electric Machinery and Drive Systems*;
- the first-order thermal model, the second-order oscillator and the cart-pendulum: Astrom and Murray, *Feedback Systems*;
- the road-load forces: Gillespie, *Fundamentals of Vehicle Dynamics* (SAE, 1992);
- the average inverter limit `Vdc / sqrt(3)`: Holtz, "Pulsewidth modulation for electronic power conversion", *Proceedings of the IEEE*,
  1994;
- the integrator: the classic Runge-Kutta method of Kutta (1901), and Hairer, Norsett and Wanner, *Solving Ordinary Differential
  Equations I*;
- the noise: the xorshift32 generator of Marsaglia, "Xorshift RNGs", *Journal of Statistical Software*, 2003, and the Box-Muller
  transform of Box and Muller, *Annals of Mathematical Statistics*, 1958;
- the standard gravity, 9.80665 m/s^2: the value adopted by the General Conference on Weights and Measures in 1901.

## Costs

The module is host-only. Its tests build the blocks with the host compiler. The sine and cosine of the sensors and the transforms come
from the C library, and the double arithmetic is the library's.
