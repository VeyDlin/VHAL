#pragma once
#include "../AtOptions.h"


// The data-mode module (stage 2). In stage 1 this is only the tag that a set lists in Modules(). A set that lists it gets no data-mode
// behaviour yet, and no error either: the prompt, the Data() URCs and SendData come with stage 2, and then this header carries them too.


struct AtDataModeTag {};


inline constexpr AtDataModeTag AtDataMode{};


template<>
inline constexpr bool AtIsModule<AtDataModeTag> = true;
