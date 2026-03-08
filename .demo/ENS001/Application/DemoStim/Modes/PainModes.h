#pragma once
#include "StimMode.h"


namespace StimModes::Pain {

// Migraine acute treatment (gammaCore-style)
// Cervical. Trigeminal nociceptor inhibition
// gammaCore / electroCore; FDA approved
constexpr StimMode MigraineBurst = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 1000.0f,     // 5kHz burst within 1ms pulse
	.currentMa        = 1.0f,        // Adjust to sensation
	.onTimeMs         = 120000,      // 2 min per dose
	.offTimeMs        = 0,
	.sessionDurationMs = 120000,     // 2 min x 3 doses per attack
	.burstMode        = true,
	.burstFreqHz      = 5000.0f,
};

// Chronic pain management
// Cymba conchae or cervical. GABA, opioid, serotonin modulation
// Meta-analysis 2024 (PMC11309651), d=0.41
constexpr StimMode Chronic = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 250.0f,
	.currentMa        = 1.0f,
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Rheumatoid arthritis
// Auricular. Cholinergic anti-inflammatory pathway
// Koopman et al. 2016
constexpr StimMode RheumatoidArthritis = {
	.frequencyHz      = 20.0f,
	.pulseWidthUs     = 500.0f,
	.currentMa        = 1.0f,
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
