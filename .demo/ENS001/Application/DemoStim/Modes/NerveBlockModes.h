#pragma once
#include "StimMode.h"


namespace StimModes::NerveBlock {

// Afferent C-fiber block via kHz frequency
// <2kHz + PW>300µs = -12dB block
// Payne et al. 2023 (Bioelectr Med)
constexpr StimMode AfferentBlock = {
	.frequencyHz      = 1000.0f,     // 1 kHz KHFAC
	.pulseWidthUs     = 500.0f,
	.currentMa        = 4.0f,
	.onTimeMs         = 0,           // Continuous, duty >70%
	.offTimeMs        = 0,
	.sessionDurationMs = 0,          // Application-dependent
	.burstMode        = false,       // Not burst — continuous kHz sine/biphasic
	.burstFreqHz      = 0,
};

// Directional stimulation (selective efferent)
// Multi-electrode cuff. Low freq block + distal activation
// Springer 2023
constexpr StimMode DirectionalEfferent = {
	.frequencyHz      = 1200.0f,     // 1.2 kHz KHFAC
	.pulseWidthUs     = 400.0f,
	.currentMa        = 4.0f,
	.onTimeMs         = 0,           // Continuous, duty >70%
	.offTimeMs        = 0,
	.sessionDurationMs = 0,
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
