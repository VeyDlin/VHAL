#pragma once
#include <Drivers/Interface/VUB/Engine/VubCrcCore.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// The bitwise engine: one bit at a time, with no table, so it costs no flash for the algorithm. It takes about eight shift-and-XOR
// steps per byte, where the table engine takes one lookup. It is stateless, like the table engine, and computes every algorithm of
// width 8, 16 or 32.
struct VubCrcBitwise {
	static constexpr bool synchronous = true;


	template<typename Algorithm>
	static constexpr bool Supports() {
		return true;
	}


	template<typename Algorithm>
	static constexpr uint32 Run(std::span<const uint8> data) {
		constexpr VubCrcParameters parameters = Algorithm::parameters;
		const uint32 remainder = VubCrcBitRun(parameters.width, parameters.polynomial, parameters.reflectIn, VubCrcStart(parameters), data);
		return VubCrcFinish(parameters, remainder);
	}
};
