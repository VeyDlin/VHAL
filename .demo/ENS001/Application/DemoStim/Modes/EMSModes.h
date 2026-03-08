#pragma once
#include "StimMode.h"


namespace StimModes::EMS {

// Muscle strengthening (NMES)
// Visible tetanic contraction. Duty cycle prevents fatigue
// Maffiuletti 2010; Filipovic et al. 2012
constexpr StimMode Strengthening = {
	.frequencyHz      = 50.0f,
	.pulseWidthUs     = 300.0f,
	.currentMa        = 40.0f,       // Adjust to visible contraction
	.onTimeMs         = 10000,       // 10s ON
	.offTimeMs        = 50000,       // 50s OFF
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Muscle endurance training
// Lower frequency, shorter rest periods
// Filipovic et al. 2012
constexpr StimMode Endurance = {
	.frequencyHz      = 30.0f,
	.pulseWidthUs     = 250.0f,
	.currentMa        = 35.0f,       // Moderate contraction
	.onTimeMs         = 10000,       // 10s ON
	.offTimeMs        = 15000,       // 15s OFF
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Muscle recovery / relaxation
// Sub-threshold twitching, promotes blood flow
// Maffiuletti 2010
constexpr StimMode Recovery = {
	.frequencyHz      = 3.0f,
	.pulseWidthUs     = 250.0f,
	.currentMa        = 15.0f,       // Gentle twitch
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1200000,    // 20 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Denervated muscle stimulation
// Very long pulses for direct muscle fiber activation
// Kern et al. 2010
constexpr StimMode Denervated = {
	.frequencyHz      = 5.0f,
	.pulseWidthUs     = 1000.0f,     // Long pulse for direct activation
	.currentMa        = 50.0f,       // High current needed
	.onTimeMs         = 4000,        // 4s ON
	.offTimeMs        = 8000,        // 8s OFF
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
