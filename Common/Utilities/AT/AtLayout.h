#pragma once
#include "AtArgument.h"
#include "AtValue.h"
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <tuple>


// The layout of the values of one command or URC in its value area. Each argument has an offset that is aligned for its type, and the
// layout gives the bytes the whole list takes. A value area holds one list at a time, so the server sizes it to the largest list.


// Rounds a size up to a multiple of an alignment.
constexpr std::size_t AtAlignUp(std::size_t size, std::size_t alignment) {
	return (size + alignment - 1) / alignment * alignment;
}


// The offset of each type in a list of types laid out one after another.
template<typename... Types>
constexpr std::array<std::size_t, sizeof...(Types)> AtOffsetsOf() {
	std::array<std::size_t, sizeof...(Types)> offsets{};
	std::size_t total = 0;
	std::size_t index = 0;

	((total = AtAlignUp(total, alignof(Types)), offsets[index] = total, total += sizeof(Types), index++), ...);
	static_cast<void>(index);
	return offsets;
}


// The bytes that a list of types takes, from its first offset to the end of its last value.
template<typename... Types>
constexpr std::size_t AtBytesOf() {
	std::size_t total = 0;

	((total = AtAlignUp(total, alignof(Types)) + sizeof(Types)), ...);
	return total;
}


// The largest reader among the types, so that one reader storage holds any of them.
template<typename... Types>
constexpr std::size_t AtReaderBytesOf() {
	return std::max({sizeof(AtValueReader<Types>)..., std::size_t{1}});
}


// The layout of an argument list, given as a tuple of AtArgument types.
template<typename Tuple>
struct AtLayoutOf;


template<typename... Arguments>
struct AtLayoutOf<std::tuple<Arguments...>> {
	static constexpr std::size_t count = sizeof...(Arguments);
	static constexpr std::array<std::size_t, sizeof...(Arguments)> offsets = AtOffsetsOf<typename Arguments::Type...>();
	static constexpr std::size_t bytes = AtBytesOf<typename Arguments::Type...>();
	static constexpr std::size_t readerBytes = AtReaderBytesOf<typename Arguments::Type...>();
};
