#pragma once
#include "StimMode.h"


namespace StimModes::Alertness {

// Cervical VNS for wakefulness (gammaCore-style)
// LC-NE activation -> alertness. Tested at 34h sleep deprivation
// McIntire et al. 2021 (Comm Biology)
constexpr StimMode CervicalBurst = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 1000.0f,     // 1ms envelope with 5kHz burst inside
	.currentMa        = 1.0f,        // Adjust to sensation
	.onTimeMs         = 360000,      // 6 min per dose
	.offTimeMs        = 0,
	.sessionDurationMs = 360000,     // 6 min, repeat as needed
	.burstMode        = true,
	.burstFreqHz      = 5000.0f,     // 5 kHz burst carrier
};

// Auricular VNS for alertness
// Cymba conchae, left ear. Alpha oscillation reduction -> cortical activation
// PMC9859411, 2023
constexpr StimMode Auricular = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 300.0f,
	.currentMa        = 0.75f,
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1200000,    // 20 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
