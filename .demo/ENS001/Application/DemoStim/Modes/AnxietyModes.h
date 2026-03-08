#pragma once
#include "StimMode.h"


namespace StimModes::Anxiety {

// Anxiolytic effect via parasympathetic activation
// Cymba conchae / tragus. HRV increase, cortisol decrease
// Burger 2019; Grolaux 2019
constexpr StimMode Anxiolytic = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 200.0f,
	.currentMa        = 1.0f,
	.onTimeMs         = 30000,       // 30s ON
	.offTimeMs        = 30000,       // 30s OFF
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Optimal HRV improvement protocol
// 10Hz/500µs = best SDNN growth. Cymba conchae, left ear
// Kim et al. 2025 (MDPI)
constexpr StimMode HrvOptimal = {
	.frequencyHz      = 10.0f,
	.pulseWidthUs     = 500.0f,
	.currentMa        = 0.5f,        // Adjust to sensation threshold
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 900000,     // 15 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
