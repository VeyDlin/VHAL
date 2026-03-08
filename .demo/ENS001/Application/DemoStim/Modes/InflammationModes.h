#pragma once
#include "StimMode.h"


namespace StimModes::Inflammation {

// Cholinergic anti-inflammatory pathway
// NTS -> DMN -> splenic nerve -> acetylcholine -> TNF-alpha suppression
// General consensus mechanism
constexpr StimMode AntiInflammatory = {
	.frequencyHz      = 5.0f,
	.pulseWidthUs     = 500.0f,
	.currentMa        = 1.0f,
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
