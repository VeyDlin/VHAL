# Plant

`Simulation::Plant<Make>` is a chain of blocks, built by a captureless lambda that returns a `Simulation::PlantBuilder<T>()`. The type of the
builder lists every block in the chain, so a block that is not in the chain has no code, no state and no signal in the image.

```cpp
#include <Utilities/Math/Simulation/Plant.h>
#include <Utilities/Math/Simulation/Motors/Pmsm.h>
#include <Utilities/Math/Simulation/Mechanics/Rotor.h>
#include <Utilities/Math/Simulation/Electrical/Inverter.h>

// The smallest chain: a PMSM with an ideal rotor and an average inverter.
using Smallest = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 2.0e-3, 0.01, 4 })
		.Add<Simulation::Rotor>()
			.Inertia(2.0e-5)
			.Viscous(1.0e-6)
		.Add<Simulation::Inverter>()
			.DcBus(14.8)
		.Substeps(20);
}>;

void Run() {
	Smallest plant;
	plant.Get<Simulation::Pmsm>().SetVoltageCommand(0.0, 1.0);
	plant.Advance(100.0e-6);
	const double speed = plant.Get<Simulation::Rotor>().Speed();
	const double current = plant.Get<Simulation::Pmsm>().CurrentQ();
	const double torque = plant.Get<Simulation::Pmsm>().Torque();
	const double angle = plant.Get<Simulation::Rotor>().ThetaM();
	const double applied = plant.Get<Simulation::Inverter>().AppliedQ();
	const std::size_t states = Smallest::stateCount;
}
```

## PlantBuilder

`Simulation::PlantBuilder<T>()` starts an empty chain. `T` is `float` or `double`; an `IQ` number type is refused with
`Simulation: a plant is float or double`.

### Add

`Add<Block>()` adds a block that takes no parameters. `Add<Block>(parameters)` adds a block with its `Parameters` struct.

```cpp
Simulation::PlantBuilder<double>()
	.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 2.0e-3, 0.01, 4 })
	.Add<Simulation::Rotor>();
```

### Substeps

`Substeps(n)` splits each control period into `n` equal RK4 steps. `n` must be positive. A builder is evaluated at compile time, so
a chain is written as a `constexpr` value, which is what the refusal messages need.

```cpp
constexpr auto chain = Simulation::PlantBuilder<double>().Add<Simulation::Rotor>().Inertia(1.0e-4).Substeps(20);
```

### Setters

Each setter sets the run-time value of its block, so the builder gives the initial value. A setter exists only when its block is
in the chain. The setters are: `Inertia`, `Viscous`, `Coulomb` (rotor); `MotorInertia`, `LoadInertia`, `Stiffness`, `Damping`
(two-mass shaft); `Mass`, `WheelRadius`, `GearRatio`, `RollingResistance`, `DragArea`, `AirDensity`, `Grade` (road load); `DcBus`
(inverter); `Torque` (constant load); `Coefficient` (quadratic load); `Counts` (encoder); `NoiseDeg`, `Seed` (magnetic sensor);
`OffsetA`, `OffsetB`, `OffsetC`, `GainError` (current sense); `Ambient`, `ThermalResistance`, `ThermalCapacitance`, `DelaySteps`,
`Power` (thermal).

```cpp
using Largest = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 3.0e-3, 0.01, 4 })
		.Add<Simulation::TwoMassShaft>()
			.MotorInertia(1.0e-4)
			.LoadInertia(4.0e-4)
			.Stiffness(100.0)
			.Damping(0.01)
		.Add<Simulation::RoadLoad>()
			.Mass(1000.0)
			.WheelRadius(0.3)
			.GearRatio(10.0)
			.RollingResistance(0.01)
			.DragArea(0.6)
			.AirDensity(1.2)
			.Grade(0.0)
		.Add<Simulation::ConstantLoad>()
			.Torque(0.0)
		.Add<Simulation::Inverter>()
			.DcBus(12.0)
		.Add<Simulation::Encoder>()
			.Counts(2048)
		.Add<Simulation::MagneticSensor>()
			.NoiseDeg(0.05)
			.Seed(5)
		.Add<Simulation::CurrentSense>()
			.OffsetA(0.002)
			.OffsetB(-0.001)
			.OffsetC(0.0)
			.GainError(0.005)
		.Substeps(40);
}>;
```

The loads of a chain are `Torque` for the constant load and `Coefficient` for the quadratic load:

```cpp
using Fan = Simulation::Plant<+[] {
	return Simulation::PlantBuilder<double>()
		.Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ 0.5, 2.0e-3, 2.0e-3, 0.01, 4 })
		.Add<Simulation::Rotor>()
			.Inertia(2.0e-5)
			.Coulomb(1.0e-6)
		.Add<Simulation::QuadraticLoad>()
			.Coefficient(1.0e-6);
}>;
```

### Chain rules

The rules are checked on the finished chain, and each one names its block:

| Rule | Message |
|---|---|
| at most one motor | `one motor per chain` |
| at most one rotor (a `Rotor` or a `TwoMassShaft`) | `one rotor per chain` |
| a rotor needs a motor | `the rotor needs a motor` |
| a motor needs a rotor | `the motor needs a rotor` |
| a road load needs a rotor | `the road needs a rotor` |
| a load needs a rotor | `the loads need a rotor` |
| an inverter needs a PMSM | `the inverter needs a PMSM` |
| an encoder needs a rotor | `the encoder needs a rotor` |
| a magnetic sensor needs a motor | `the magnetic sensor needs a motor` |
| a current sense needs a motor | `the current sense needs a motor` |

## Plant

`Plant<Make>` holds the chain, its states and the bus. The constructor writes the initial states of the blocks and latches the
outputs at the initial state.

### Advance

`Advance(period)` integrates the plant over one control period, split into the builder's substeps, then runs each block's
`Commit` and latches the outputs. The outputs read after `Advance` are those at the end of the period.

```cpp
Smallest plant;
plant.Advance(100.0e-6);
```

### Get

`Get<Block>()` returns the block of a kind in the chain. Each kind is in the chain at most once.

```cpp
Smallest plant;
Simulation::Rotor<double>& rotor = plant.Get<Simulation::Rotor>();
```

### Road, EncoderBlock and CurrentSenseBlock

These name the blocks whose absence has its own message: `Road()` (`the chain has no Road block`), `EncoderBlock()` (`the chain
has no Encoder block`) and `CurrentSenseBlock()` (`no phase current: the chain has no CurrentSense block`).

```cpp
Largest large;
large.Advance(100.0e-6);
const double torque = large.Road().LoadTorque();
const std::uint32_t counts = large.EncoderBlock().Counts();
const double phase = large.CurrentSenseBlock().PhaseA();
```

### stateCount

`Plant::stateCount` is the number of RK4 states of the chain: the sum of the states of its blocks. The smallest chain above has
five (the PMSM's three and the rotor's two).

## The block contract

Each block provides:

- `states`, the number of RK4 states it owns (0 for a stateless block);
- `stage`, 0, 1 or 2. The publish pass runs every stage-0 block first, then the stage-1 blocks, then the stage-2 blocks. A block
  that reads another block's signal sits at a later stage. Each step runs the publish pass, then the derivatives;
- `Signals`, the struct of the signals the block publishes (it may be empty);
- `Publish(state, bus)`, which writes the block's signals;
- `Derivative(state, bus, derivative)`, which writes the derivatives of the block's states;
- `Initialize(state)`, which writes the initial states;
- `Latch(bus)` (optional), which copies the signals to the outputs once per `Advance`;
- `Commit()` (optional), which runs once per `Advance` after the integration.

The bus is one struct that inherits the `Signals` of every block in the chain. A signal that a block reads only when another
block publishes it is read through a `requires` clause, so the block works with or without that block.
