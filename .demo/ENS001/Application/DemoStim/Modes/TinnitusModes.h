#pragma once
#include "StimMode.h"


namespace StimModes::Tinnitus {

// tVNS paired with sound therapy
// Tragus / cymba conchae, left ear
// Auditory cortex neuroplasticity, paired with tones
// Engineer 2011; Shim 2015
constexpr StimMode PairedSoundTherapy = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 500.0f,
	.currentMa        = 0.8f,        // Adjust to below pain threshold
	.onTimeMs         = 0,           // Continuous (paired with tones)
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
