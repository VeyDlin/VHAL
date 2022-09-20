#pragma once
#include <cstddef>
#include <cstring>

using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;


#define _ANoinline 			__attribute__((noinline))
#define _AInline 			__attribute__((always_inline)) inline
#define _ANoreturn	 		__attribute__((__noreturn__))
#define _AUsed 				__attribute__((used))
#define _AWeak 				__attribute__((weak))

#define _APackedAligned(x)	__attribute__((packed, aligned(x)))
#define _APacked 			__attribute__((__packed__))
#define _APacked1 			_APackedAligned(1)
#define _APacked2 			_APackedAligned(2)
#define _APacked4 			_APackedAligned(4)


#define __FILENAME_ONLY__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)







