#pragma once
#include <cstddef>


// Test-only probe of the closure stack of the captures loop. The hook REGEX_TRACE_FRAMES is defined here before Regex.h is included, so
// every push of the loop reports its depth to the probe. Including this header is the only way a test gets the probe, which keeps the
// order right. Release builds do not include it, and the hook is empty there.
namespace RegexProbe {


// The deepest closure stack seen since the last Reset.
inline std::size_t highWater = 0;


inline void Reset() {
	highWater = 0;
}


inline void Record(std::size_t depth) {
	if (depth > highWater) {
		highWater = depth;
	}
}


}


#define REGEX_TRACE_FRAMES(depth) ::RegexProbe::Record(depth)
#include <Utilities/Regex/Regex.h>
