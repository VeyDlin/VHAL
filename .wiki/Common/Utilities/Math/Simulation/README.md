# Simulation

Physical plant models for closed-loop host tests and PC programs. The models are double precision by default, integrated by a
fixed-step classic Runge-Kutta of order four with a substep count chosen by the caller. The module is header-only and sits under
`Utilities/Math`. It is for the PC: it has no size table, and a plant in firmware is not a goal.

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
| `TwoMassShaft` | 4 | 0 | torque, constant and road loads | [TwoMassShaft](Mechanics/TwoMassShaft.md) |
| `RoadLoad` | 0 | 1 | speed | [RoadLoad](Mechanics/RoadLoad.md) |
| `ConstantLoad`, `QuadraticLoad` | 0 | 1 | speed (quadratic) | [Loads](Mechanics/Loads.md) |
| `Inverter` | 0 | 1 | voltage | [Inverter](Electrical/Inverter.md) |
| `Encoder` | 0 | 2 | thetaM | [Encoder](Sensors/Encoder.md) |
| `MagneticSensor` | 0 | 2 | thetaE | [MagneticSensor](Sensors/MagneticSensor.md) |
| `CurrentSense` | 0 | 2 | currents, thetaE | [CurrentSense](Sensors/CurrentSense.md) |
| `FirstOrderThermal` | 1: rise | 0 | none | [FirstOrderThermal](Thermal/FirstOrderThermal.md) |
| `MassSpringDamper` | 2 | 0 | none | [MassSpringDamper](Mechanics/MassSpringDamper.md) |
| `CartPendulum` | 4 | 0 | none | [CartPendulum](Mechanics/CartPendulum.md) |

The stage orders the publish pass, and it is listed in [Plant](Plant.md).

## Chain rules

The builder accepts its blocks in any order. The rules are checked on the finished chain, and each message names the block:

- one motor per chain, and one rotor per chain (a `Rotor` or a `TwoMassShaft`);
- a rotor needs a motor, and a motor needs a rotor;
- a road load, a load, and an encoder need a rotor;
- an inverter needs a PMSM;
- a magnetic sensor and a current sense need a motor.

A setter exists only when its block is in the chain. A value that is refused at compile time (a zero counts, a zero delay, a
non-positive inertia, an `IQ` number type) stops the build with its message.

## Parameter sets

The library has no motor parameter set. The sources in the research reviewed for this module give no complete set: the
motulator example has no pole-pair count, the forum hub motor has no pole-pair count and no separate `Ld` and `Lq`, the RE-280
brushed motor has no inductance, inertia or friction, and no vehicle data was found. A parameter set is added only with its
source and its derivation in a comment. The examples on these pages use test values chosen for the checks; they are not
motor parameters.

## Equations and sources

The equation of each block is in its page, with the source it follows. The amplitude-invariant dq model of the PMSM follows
Krause, Wasynczuk, Sudhoff and Pekarek, *Analysis of Electric Machinery and Drive Systems*. The road-load equation follows the
Ecomodder wiki. The brushed motor follows the Northwestern University note on brushed DC motor theory. The integrator is the
classic Runge-Kutta method of Kutta (1901).

## Costs

The module is host-only. Its tests build the blocks with the host compiler. The sine and cosine of the sensors and the
transforms come from the C library, and the double arithmetic is the library's.
