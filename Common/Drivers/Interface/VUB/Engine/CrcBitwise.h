#pragma once
#include <Drivers/Interface/VUB/Engine/CrcCore.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>

namespace VUB {


// The bitwise engine: one bit at a time, with no table, so it costs no flash for the algorithm. It takes about eight shift-and-XOR
// steps per byte, where the table engine takes one lookup. It is stateless, like the table engine, and computes every algorithm of
// width 8, 16 or 32.
struct CrcBitwise {
	static constexpr bool synchronous = true;


	template<typename Algorithm>
	static constexpr bool Supports() {
		return true;
	}


	template<typename Algorithm>
	static constexpr uint32 Run(std::span<const uint8> data) {
		constexpr CrcParameters parameters = Algorithm::parameters;
		const uint32 remainder = CrcBitRun(parameters.width, parameters.polynomial, parameters.reflectIn, CrcStart(parameters), data);
		return CrcFinish(parameters, remainder);
	}
};

}
