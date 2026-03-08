#pragma once
#include "StimMode.h"


namespace StimModes::Sleep {

// General sleep improvement
// Auricular concha, bilateral. Parasympathetic tone increase
// Bretherton 2019; Jiao 2020
constexpr StimMode Improvement = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 200.0f,
	.currentMa        = 1.0f,
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1200000,    // 20 min (x2/day recommended)
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Primary insomnia protocol
// Cymba conchae + cavum, bilateral. 50% patients: PSQI reduction >= 50%
// Xuanwu Hospital Beijing 2022
constexpr StimMode Insomnia = {
	.frequencyHz      = 20.0f,
	.pulseWidthUs     = 200.0f,
	.currentMa        = 1.0f,
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1200000,    // 20 min (x2/day, 1 month course)
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
