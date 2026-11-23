#pragma once
#include "../Doubles.h"
#include "../StepDoubles.h"
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Utilities/DataTypes.h>
#include <span>


// The receive handler and the clock every snippet gives its chain, so each snippet fails only on the rule it shows.
inline void SnippetReceive(const VUB::Meta&, std::span<const uint8>) {
}


inline uint32 SnippetClock() {
	return 0;
}
