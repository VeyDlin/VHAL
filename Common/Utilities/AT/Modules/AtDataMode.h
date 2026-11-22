#pragma once
#include "../AtOptions.h"
#include <Utilities/DataTypes.h>
#include <cstddef>


// The data-mode module. A set lists it with Modules(AtDataMode), and then the set has two things that carry raw bytes:
//   - a prompt command: a write form with .Prompt(). The client sends it with SendData, and after the module's '>' prompt the client writes
//     the data of the request, which has no line end. Its final lines, such as SEND OK, are declared with .Finals().
//   - a URC with Data() as its last argument. The argument before Data() is the length, an unsigned integer. After the length comes ':'
//     and then exactly that many raw bytes, which never form lines.
// A set without the module has neither, and no code or RAM for them. The module tag is a type, so a set that does not list it gets no
// code for either.


struct AtDataModeTag {
};


inline constexpr AtDataModeTag AtDataMode{};


template<>
inline constexpr bool AtIsModule<AtDataModeTag> = true;


// The number of raw bytes a client holds for one Data() chunk. A longer run is delivered in several chunks.
inline constexpr std::size_t AtChunkBytes = 32;


// The Data() argument of a URC, written as the last argument of its list. It has no value of its own: the type of the length before it is
// the type of the chunk offset that the handler gets.
struct AtDataDecl {
};


constexpr AtDataDecl Data() {
	return AtDataDecl{};
}


// The Data() of a URC that is kept in its description, with the type of its length.
template<typename Length>
struct AtDataLength {
	using Type = Length;
};


// The Data() of a URC that has none. A URC without Data() is an AtNoData.
struct AtNoData {
};


// True for the Data() of a URC with a length.
template<typename Data>
inline constexpr bool AtIsDataLength = false;


template<typename Length>
inline constexpr bool AtIsDataLength<AtDataLength<Length>> = true;
