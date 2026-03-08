#pragma once
#include "StimMode.h"


namespace StimModes::Cognitive {

// Associative memory enhancement
// Cymba conchae / tragus. NE release in hippocampus -> LTP
// Jacobs et al. 2015
constexpr StimMode Memory = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 300.0f,
	.currentMa        = 0.5f,
	.onTimeMs         = 0,           // Continuous during task
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Working memory improvement
// Cymba conchae. Gate closing mechanism enhancement
// Konjusha et al. 2023
constexpr StimMode WorkingMemory = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 250.0f,
	.currentMa        = 0.5f,
	.onTimeMs         = 0,           // Continuous during task
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Divergent thinking / creativity
// Left ear. More original solutions
// Colzato et al. 2018
constexpr StimMode DivergentThinking = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 250.0f,
	.currentMa        = 0.5f,
	.onTimeMs         = 0,           // Continuous during task
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Sensory perception improvement (gammaCore-style)
// Cervical. Auditory and visual discrimination improvement
// Sci Reports 2024
constexpr StimMode SensoryPerception = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 1000.0f,     // 5kHz burst
	.currentMa        = 1.0f,        // Adjust to sensation
	.onTimeMs         = 0,           // Continuous during task
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = true,
	.burstFreqHz      = 5000.0f,
};

}
