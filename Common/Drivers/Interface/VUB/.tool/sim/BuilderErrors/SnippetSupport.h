#pragma once
#include "../VubDoubles.h"
#include "../VubStepDoubles.h"
#include <Drivers/Interface/VUB/TestKit/VubLoopback.h>
#include <Drivers/Interface/VUB/Vub.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Utilities/DataTypes.h>
#include <span>


// The receive handler and the clock every snippet gives its chain, so each snippet fails only on the rule it shows.
inline void SnippetReceive(const VubMeta&, std::span<const uint8>) {
}


inline uint32 SnippetClock() {
	return 0;
}
