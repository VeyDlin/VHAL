#pragma once
#include <Utilities/Math/Simulation/Integrator.h>
#include <array>
#include <cstddef>


// Runs one block alone, for the block checks. The bus is the block's own bus: the block's Signals (which the block writes in
// Publish) and the inputs the test sets, which are read in Derivative. The inputs are copied into the bus before each evaluation,
// so the block's outputs never feed back into the inputs.
template<typename Block, typename Bus>
class BlockRun {
public:
	Block block;
	Bus inputs{};
	std::array<double, Block::states> state{};


	// The block starts from the initial states it was set to, as a plant does.
	explicit BlockRun(Block value) : block(value) {
		block.Initialize(state.data());
	}


	void Step(double step) {
		Evaluator evaluator{ this };
		Simulation::Integrator<double, Block::states>::Step(state, step, evaluator);
	}


	void Evaluate(const double* stateValues, double* derivativeValues) const {
		Bus bus = inputs;
		block.Publish(stateValues, bus);
		block.Derivative(stateValues, bus, derivativeValues);
	}


	// The bus after the block published from the current state.
	Bus Published() const {
		Bus bus = inputs;
		block.Publish(state.data(), bus);
		return bus;
	}


private:
	struct Evaluator {
		const BlockRun* owner;


		void operator()(const double* stateValues, double* derivativeValues) const {
			owner->Evaluate(stateValues, derivativeValues);
		}
	};
};
