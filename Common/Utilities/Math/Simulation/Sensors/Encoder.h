#pragma once
#include <Utilities/Math/Simulation/Angles.h>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>


// An incremental encoder. It counts the shaft angle in a number of counts per mechanical turn: the count is floor(turns * Counts),
// with the turns taken modulo one turn, so a count is always below Counts. The encoder is stateless.
namespace Simulation {


template<std::floating_point T>
class Encoder {
public:
	struct Signals {
		std::uint32_t encoderCounts = 0;
	};


	static constexpr std::size_t states = 0;
	static constexpr int stage = 2;


private:
	std::uint32_t counts = 0;
	std::uint32_t latchedCounts = 0;


public:
	constexpr Encoder() = default;


	// The counts per mechanical turn. Set by the builder, which refuses zero.
	constexpr void SetCounts(std::uint32_t value) {
		counts = value;
	}


	template<typename Bus>
	void Publish(const T*, Bus& bus) const {
		if (counts == 0) {
			bus.encoderCounts = 0;
			return;
		}

		const T turns = WrapAngle(bus.thetaM) / TwoPi<T>();
		std::uint32_t count = static_cast<std::uint32_t>(std::floor(turns * static_cast<T>(counts)));

		if (count >= counts) {
			count = counts - 1;
		}

		bus.encoderCounts = count;
	}


	template<typename Bus>
	constexpr void Derivative(const T*, const Bus&, T*) const {
	}


	constexpr void Initialize(T*) const {
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latchedCounts = bus.encoderCounts;
	}


	// The count at the end of the last period.
	constexpr std::uint32_t Counts() const {
		return latchedCounts;
	}
};


}
