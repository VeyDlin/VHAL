#pragma once
#include "StimMode.h"


namespace StimModes::TENS {

// Conventional TENS — gate control theory pain relief
// High frequency, low intensity at sensory threshold
// Sluka & Walsh 2003; Johnson 2007
constexpr StimMode Conventional = {
	.frequencyHz      = 100.0f,
	.pulseWidthUs     = 100.0f,
	.currentMa        = 20.0f,       // Adjust to sensory threshold
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Acupuncture-like TENS (AL-TENS)
// Low frequency, high intensity at motor threshold
// Endorphin-mediated analgesia
// Sluka & Walsh 2003; Vance et al. 2014
constexpr StimMode AcupunctureLike = {
	.frequencyHz      = 2.0f,
	.pulseWidthUs     = 250.0f,
	.currentMa        = 30.0f,       // Adjust to motor threshold (visible twitch)
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Burst TENS
// 2 Hz bursts containing internal 100 Hz trains
// Combines gate control + endorphin mechanisms
// Johnson 2007
constexpr StimMode Burst = {
	.frequencyHz      = 2.0f,
	.pulseWidthUs     = 200.0f,
	.currentMa        = 25.0f,       // Comfortable intensity
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = true,
	.burstFreqHz      = 100.0f,      // 100 Hz internal burst
};

// Brief Intense TENS
// High frequency, high intensity — for acute/procedural pain
// Short sessions, highest tolerable intensity
// Johnson 2007
constexpr StimMode BriefIntense = {
	.frequencyHz      = 120.0f,
	.pulseWidthUs     = 200.0f,
	.currentMa        = 40.0f,       // Highest tolerable
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 900000,     // 15 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
