#pragma once
#include <Utilities/CompileTime/Require.h>
#include <Utilities/Math/Simulation/Integrator.h>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>


// A plant is a chain of blocks. The chain is written with a builder, given to Plant as a captureless lambda, so the type of the
// chain lists every block in it. A block that is not in the chain has no code, no state and no signal in the image.
//
//   using Smallest = Simulation::Plant<+[] {
//       return Simulation::PlantBuilder<double>()
//           .Add<Simulation::Pmsm>(Simulation::Pmsm<double>::Parameters{ ... })
//           .Add<Simulation::Rotor>()
//               .Inertia(2.0e-5)
//           .Substeps(20);
//   }>;
//
// Every block has the same contract (see the wiki page of this file):
//   - states: the number of RK4 states the block owns, 0 when it has none;
//   - stage: 0, 1 or 2. Publish runs for every block of stage 0 first, then stage 1, then stage 2, before any derivative, so
//     a block that reads another block's signal sits at a later stage;
//   - Publish(state, bus): writes the bus signals of the block (const);
//   - Derivative(state, bus, derivative): writes the derivatives of the block's own states (const);
//   - Initialize(state): writes the initial states, from the values the block was set to (const);
//   - Latch(bus): copies the bus signals to the block's outputs, once per Advance (optional);
//   - Commit(): runs once per Advance, after the integration (optional).
// The bus is the struct of the Signals of every block in the chain. An optional signal, one that a block reads only when another
// block publishes it, is read with a requires clause.
namespace Simulation {


// The blocks, declared here so that the builder can name them. A user includes the header of each block it adds.
template<std::floating_point T>
class Pmsm;

template<std::floating_point T>
class BrushedDc;

template<std::floating_point T>
class Rotor;

template<std::floating_point T>
class TwoMassShaft;

template<std::floating_point T>
class RoadLoad;

template<std::floating_point T>
class ConstantLoad;

template<std::floating_point T>
class QuadraticLoad;

template<std::floating_point T>
class Inverter;

template<std::floating_point T>
class Encoder;

template<std::floating_point T>
class MagneticSensor;

template<std::floating_point T>
class CurrentSense;

template<std::floating_point T>
class FirstOrderThermal;


template<typename... Blocks>
struct BlockList;


// The end of a chain.
template<>
struct BlockList<> {
	static constexpr std::size_t stateCount = 0;


	template<typename Target>
	static constexpr std::size_t Count() {
		return 0;
	}


	template<int Stage, typename Real, typename Bus>
	constexpr void Publish(const Real*, Bus&) const {
	}


	template<typename Real, typename Bus>
	constexpr void Derivative(const Real*, const Bus&, Real*) const {
	}


	template<typename Real>
	constexpr void Initialize(Real*) const {
	}


	template<typename Bus>
	void Latch(const Bus&) {
	}


	void Commit() {
	}
};


// A chain of one or more blocks: the first block, then the rest of the chain. The states of a block come before the states of
// the blocks after it.
template<typename Head, typename... Tail>
struct BlockList<Head, Tail...> {
	static constexpr std::size_t stateCount = Head::states + BlockList<Tail...>::stateCount;


	Head head;
	BlockList<Tail...> tail;


	constexpr BlockList() = default;


	constexpr BlockList(Head headValue, BlockList<Tail...> tailValue) : head(headValue), tail(tailValue) {
	}


	template<typename Target>
	static constexpr std::size_t Count() {
		return (std::is_same_v<Target, Head> ? 1 : 0) + BlockList<Tail...>::template Count<Target>();
	}


	template<typename Target>
	constexpr Target& Get() {
		if constexpr (std::is_same_v<Target, Head>) {
			return head;
		} else {
			return tail.template Get<Target>();
		}
	}


	template<int Stage, typename Real, typename Bus>
	constexpr void Publish(const Real* state, Bus& bus) const {
		if constexpr (Head::stage == Stage) {
			head.Publish(state, bus);
		}

		tail.template Publish<Stage>(state + Head::states, bus);
	}


	template<typename Real, typename Bus>
	constexpr void Derivative(const Real* state, const Bus& bus, Real* derivative) const {
		head.Derivative(state, bus, derivative);
		tail.Derivative(state + Head::states, bus, derivative + Head::states);
	}


	template<typename Real>
	constexpr void Initialize(Real* state) const {
		head.Initialize(state);
		tail.Initialize(state + Head::states);
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		if constexpr (requires { head.Latch(bus); }) {
			head.Latch(bus);
		}

		tail.Latch(bus);
	}


	void Commit() {
		if constexpr (requires { head.Commit(); }) {
			head.Commit();
		}

		tail.Commit();
	}
};


// Appends a block to the end of a chain.
template<typename New>
constexpr BlockList<New> AppendBlock(BlockList<>, New value) {
	return BlockList<New>(value, BlockList<>{});
}


template<typename Head, typename... Tail, typename New>
constexpr BlockList<Head, Tail..., New> AppendBlock(BlockList<Head, Tail...> list, New value) {
	return BlockList<Head, Tail..., New>(list.head, AppendBlock(list.tail, value));
}


// The bus of a chain: one struct that inherits the Signals of every block. An empty Signals takes no space.
template<typename... Blocks>
struct PlantBus : Blocks::Signals... {
};


template<typename Chain>
struct PlantBusOf;


template<typename... Blocks>
struct PlantBusOf<BlockList<Blocks...>> {
	using Type = PlantBus<Blocks...>;
};


// The fluent builder of a chain. Each Add returns a new builder whose type lists the block, so the plant knows the chain at compile
// time. A setter exists only when its block is in the chain, and it sets the block's run-time value; the builder gives the initial
// value. The number type is the template argument: float or double (a plant on IQ is refused). A chain starts with PlantBuilder.
template<typename T, typename... Blocks>
class PlantChainBuilder {
	static_assert(std::is_floating_point_v<T>, "Simulation: a plant is float or double");


public:
	using Real = T;
	using ChainType = BlockList<Blocks...>;


	ChainType blocks;
	std::size_t substepCount = 1;


	constexpr PlantChainBuilder() = default;


	constexpr PlantChainBuilder(ChainType chain, std::size_t count) : blocks(chain), substepCount(count) {
	}


	// Adds a block that takes no parameters.
	template<template<std::floating_point> class Block>
	constexpr PlantChainBuilder<T, Blocks..., Block<T>> Add() const {
		return PlantChainBuilder<T, Blocks..., Block<T>>(AppendBlock(blocks, Block<T>()), substepCount);
	}


	// Adds a block with its parameters.
	template<template<std::floating_point> class Block>
	constexpr PlantChainBuilder<T, Blocks..., Block<T>> Add(typename Block<T>::Parameters parameters) const {
		return PlantChainBuilder<T, Blocks..., Block<T>>(AppendBlock(blocks, Block<T>(parameters)), substepCount);
	}


	// The number of RK4 steps of one control period. A period is split into this many equal steps.
	constexpr PlantChainBuilder Substeps(std::size_t count) const {
		Require<"Simulation: substeps must be positive">(count > 0);

		PlantChainBuilder result(*this);
		result.substepCount = count;
		return result;
	}


	// The setters of the blocks. Each one exists only with its block.
	constexpr PlantChainBuilder Inertia(T value) const requires (ChainType::template Count<Rotor<T>>() == 1) {
		Require<"Simulation: the rotor inertia must be positive">(value > static_cast<T>(0));

		PlantChainBuilder result(*this);
		result.blocks.template Get<Rotor<T>>().SetInertia(value);
		return result;
	}


	constexpr PlantChainBuilder Viscous(T value) const requires (ChainType::template Count<Rotor<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<Rotor<T>>().SetViscous(value);
		return result;
	}


	constexpr PlantChainBuilder Coulomb(T value) const requires (ChainType::template Count<Rotor<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<Rotor<T>>().SetCoulomb(value);
		return result;
	}


	constexpr PlantChainBuilder MotorInertia(T value) const requires (ChainType::template Count<TwoMassShaft<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<TwoMassShaft<T>>().SetMotorInertia(value);
		return result;
	}


	constexpr PlantChainBuilder LoadInertia(T value) const requires (ChainType::template Count<TwoMassShaft<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<TwoMassShaft<T>>().SetLoadInertia(value);
		return result;
	}


	constexpr PlantChainBuilder Stiffness(T value) const requires (ChainType::template Count<TwoMassShaft<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<TwoMassShaft<T>>().SetStiffness(value);
		return result;
	}


	constexpr PlantChainBuilder Damping(T value) const requires (ChainType::template Count<TwoMassShaft<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<TwoMassShaft<T>>().SetDamping(value);
		return result;
	}


	constexpr PlantChainBuilder Mass(T value) const requires (ChainType::template Count<RoadLoad<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<RoadLoad<T>>().SetMass(value);
		return result;
	}


	constexpr PlantChainBuilder WheelRadius(T value) const requires (ChainType::template Count<RoadLoad<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<RoadLoad<T>>().SetWheelRadius(value);
		return result;
	}


	constexpr PlantChainBuilder GearRatio(T value) const requires (ChainType::template Count<RoadLoad<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<RoadLoad<T>>().SetGearRatio(value);
		return result;
	}


	constexpr PlantChainBuilder RollingResistance(T value) const requires (ChainType::template Count<RoadLoad<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<RoadLoad<T>>().SetRollingResistance(value);
		return result;
	}


	constexpr PlantChainBuilder DragArea(T value) const requires (ChainType::template Count<RoadLoad<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<RoadLoad<T>>().SetDragArea(value);
		return result;
	}


	constexpr PlantChainBuilder AirDensity(T value) const requires (ChainType::template Count<RoadLoad<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<RoadLoad<T>>().SetAirDensity(value);
		return result;
	}


	constexpr PlantChainBuilder Grade(T value) const requires (ChainType::template Count<RoadLoad<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<RoadLoad<T>>().SetGrade(value);
		return result;
	}


	constexpr PlantChainBuilder DcBus(T value) const requires (ChainType::template Count<Inverter<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<Inverter<T>>().SetDcBus(value);
		return result;
	}


	constexpr PlantChainBuilder Torque(T value) const requires (ChainType::template Count<ConstantLoad<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<ConstantLoad<T>>().SetTorque(value);
		return result;
	}


	constexpr PlantChainBuilder Coefficient(T value) const requires (ChainType::template Count<QuadraticLoad<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<QuadraticLoad<T>>().SetCoefficient(value);
		return result;
	}


	constexpr PlantChainBuilder Counts(std::uint32_t value) const requires (ChainType::template Count<Encoder<T>>() == 1) {
		Require<"Simulation: counts must be positive">(value > 0);

		PlantChainBuilder result(*this);
		result.blocks.template Get<Encoder<T>>().SetCounts(value);
		return result;
	}


	constexpr PlantChainBuilder NoiseDeg(T value) const requires (ChainType::template Count<MagneticSensor<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<MagneticSensor<T>>().SetNoiseDeg(value);
		return result;
	}


	constexpr PlantChainBuilder Seed(std::uint32_t value) const requires (ChainType::template Count<MagneticSensor<T>>() == 1) {
		Require<"Simulation: the seed must not be zero">(value != 0);

		PlantChainBuilder result(*this);
		result.blocks.template Get<MagneticSensor<T>>().SetSeed(value);
		return result;
	}


	constexpr PlantChainBuilder OffsetA(T value) const requires (ChainType::template Count<CurrentSense<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<CurrentSense<T>>().SetOffsetA(value);
		return result;
	}


	constexpr PlantChainBuilder OffsetB(T value) const requires (ChainType::template Count<CurrentSense<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<CurrentSense<T>>().SetOffsetB(value);
		return result;
	}


	constexpr PlantChainBuilder OffsetC(T value) const requires (ChainType::template Count<CurrentSense<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<CurrentSense<T>>().SetOffsetC(value);
		return result;
	}


	constexpr PlantChainBuilder GainError(T value) const requires (ChainType::template Count<CurrentSense<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<CurrentSense<T>>().SetGainError(value);
		return result;
	}


	constexpr PlantChainBuilder Ambient(T value) const requires (ChainType::template Count<FirstOrderThermal<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<FirstOrderThermal<T>>().SetAmbient(value);
		return result;
	}


	constexpr PlantChainBuilder ThermalResistance(T value) const requires (ChainType::template Count<FirstOrderThermal<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<FirstOrderThermal<T>>().SetResistance(value);
		return result;
	}


	constexpr PlantChainBuilder ThermalCapacitance(T value) const requires (ChainType::template Count<FirstOrderThermal<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<FirstOrderThermal<T>>().SetCapacitance(value);
		return result;
	}


	constexpr PlantChainBuilder DelaySteps(std::size_t value) const requires (ChainType::template Count<FirstOrderThermal<T>>() == 1) {
		Require<"Simulation: depth must be positive">(value > 0);
		Require<"Simulation: the delay is longer than the line">(value <= FirstOrderThermal<T>::maxDelaySteps);

		PlantChainBuilder result(*this);
		result.blocks.template Get<FirstOrderThermal<T>>().SetDelaySteps(value);
		return result;
	}


	constexpr PlantChainBuilder Power(T value) const requires (ChainType::template Count<FirstOrderThermal<T>>() == 1) {
		PlantChainBuilder result(*this);
		result.blocks.template Get<FirstOrderThermal<T>>().SetPower(value);
		return result;
	}
};


// An empty chain of the number type T, the start of a plant's builder: Simulation::PlantBuilder<double>().Add<...>()...
template<typename T>
constexpr PlantChainBuilder<T> PlantBuilder() {
	return PlantChainBuilder<T>();
}


// The plant: the chain of a builder, its states and its integration. Make is a captureless lambda that returns the builder.
template<auto Make>
class Plant {
public:
	using BuilderType = decltype(Make());
	using Real = typename BuilderType::Real;
	using Chain = typename BuilderType::ChainType;
	using Bus = typename PlantBusOf<Chain>::Type;


	static constexpr BuilderType config = Make();
	static constexpr std::size_t stateCount = Chain::stateCount;


	static constexpr std::size_t motorCount =
		Chain::template Count<Simulation::Pmsm<Real>>() + Chain::template Count<Simulation::BrushedDc<Real>>();
	static constexpr std::size_t rotorCount =
		Chain::template Count<Simulation::Rotor<Real>>() + Chain::template Count<Simulation::TwoMassShaft<Real>>();
	static constexpr std::size_t loadCount =
		Chain::template Count<Simulation::ConstantLoad<Real>>() + Chain::template Count<Simulation::QuadraticLoad<Real>>();
	static constexpr std::size_t roadCount = Chain::template Count<Simulation::RoadLoad<Real>>();
	static constexpr std::size_t inverterCount = Chain::template Count<Simulation::Inverter<Real>>();
	static constexpr std::size_t encoderCount = Chain::template Count<Simulation::Encoder<Real>>();
	static constexpr std::size_t magneticCount = Chain::template Count<Simulation::MagneticSensor<Real>>();
	static constexpr std::size_t senseCount = Chain::template Count<Simulation::CurrentSense<Real>>();
	static constexpr std::size_t pmsmCount = Chain::template Count<Simulation::Pmsm<Real>>();


	static_assert(motorCount <= 1, "Simulation: one motor per chain");
	static_assert(rotorCount <= 1, "Simulation: one rotor per chain");
	static_assert(rotorCount == 0 || motorCount == 1, "Simulation: the rotor needs a motor");
	static_assert(motorCount == 0 || rotorCount == 1, "Simulation: the motor needs a rotor");
	static_assert(roadCount == 0 || rotorCount == 1, "Simulation: the road needs a rotor");
	static_assert(loadCount == 0 || rotorCount == 1, "Simulation: the loads need a rotor");
	static_assert(inverterCount == 0 || Chain::template Count<Simulation::Pmsm<Real>>() == 1, "Simulation: the inverter needs a PMSM");
	static_assert(encoderCount == 0 || rotorCount == 1, "Simulation: the encoder needs a rotor");
	static_assert(magneticCount == 0 || pmsmCount == 1, "Simulation: the magnetic sensor needs a PMSM");
	static_assert(senseCount == 0 || pmsmCount == 1, "Simulation: the current sense needs a PMSM");


	Plant() : chain(config.blocks), substeps(config.substepCount), state{} {
		chain.Initialize(state.data());
		Latch();
	}


	// Advances the plant by one control period. The period is split into the builder's substeps.
	void Advance(Real period) {
		const Real step = period / static_cast<Real>(substeps);
		Evaluator evaluator{ this };

		for (std::size_t i = 0; i < substeps; i++) {
			Integrator<Real, stateCount>::Step(state, step, evaluator);
		}

		chain.Commit();
		Latch();
	}


	// The block of a kind in the chain. Each kind is in the chain at most once, so Get<Pmsm>() names the motor.
	template<template<std::floating_point> class Block>
	Block<Real>& Get() {
		static_assert(Chain::template Count<Block<Real>>() == 1, "Simulation: the chain has no such block");
		return chain.template Get<Block<Real>>();
	}


	// The road load of the chain. Its block is named, so that its absence has a message.
	Simulation::RoadLoad<Real>& Road() {
		static_assert(roadCount == 1, "Simulation: the chain has no Road block");
		return chain.template Get<Simulation::RoadLoad<Real>>();
	}


	Simulation::Encoder<Real>& EncoderBlock() {
		static_assert(encoderCount == 1, "Simulation: the chain has no Encoder block");
		return chain.template Get<Simulation::Encoder<Real>>();
	}


	Simulation::CurrentSense<Real>& CurrentSenseBlock() {
		static_assert(senseCount == 1, "Simulation: no phase current: the chain has no CurrentSense block");
		return chain.template Get<Simulation::CurrentSense<Real>>();
	}


private:
	// The derivative function of the integrator, as a type: the plant evaluates its chain for a state.
	struct Evaluator {
		Plant* owner;


		void operator()(const Real* stateValues, Real* derivativeValues) const {
			owner->Evaluate(stateValues, derivativeValues);
		}
	};


	// Runs every publish stage in order.
	void PublishAll(const Real* stateValues, Bus& bus) const {
		chain.template Publish<0>(stateValues, bus);
		chain.template Publish<1>(stateValues, bus);
		chain.template Publish<2>(stateValues, bus);
	}


	void Evaluate(const Real* stateValues, Real* derivativeValues) const {
		Bus bus{};
		PublishAll(stateValues, bus);
		chain.Derivative(stateValues, bus, derivativeValues);
	}


	void Latch() {
		Bus bus{};
		PublishAll(state.data(), bus);
		chain.Latch(bus);
	}


	Chain chain;
	std::size_t substeps;
	std::array<Real, stateCount> state;
};


}
