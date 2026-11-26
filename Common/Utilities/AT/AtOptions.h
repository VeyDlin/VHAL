#pragma once
#include "AtError.h"
#include <Utilities/DataTypes.h>
#include <cstddef>


// The set-level options: the default timeout, and the module tags. A module is a tag object, such as AtDataMode, that a set lists in
// Modules(). Each module header specializes AtIsModule for its tag. A module the set does not list adds no code.


// The default timeout of a set, given by AtDefaults().TimeoutMs(n). Without it a set uses AtFallbackTimeoutMs.
constexpr uint32 AtFallbackTimeoutMs = 1000;


struct AtDefaults {
	uint32 timeoutMs = 0;


	constexpr AtDefaults TimeoutMs(uint32 milliseconds) const {
		Require<"AT:a timeout of 0">(milliseconds > 0);

		AtDefaults changed = *this;
		changed.timeoutMs = milliseconds;
		return changed;
	}
};


// True for a module tag. The primary template is false, so an object that is not a module cannot be listed.
template<typename Tag>
inline constexpr bool AtIsModule = false;
