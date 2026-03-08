#pragma once
#include "StimMode.h"


namespace StimModes::Appetite {

// Cervical VNS — weight loss side effect
// Weight loss proportional to initial BMI
// Pardo et al. (PMC2365729)
constexpr StimMode CervicalStimulation = {
	.frequencyHz      = 30.0f,
	.pulseWidthUs     = 500.0f,
	.currentMa        = 1.0f,
	.onTimeMs         = 30000,       // 30s ON
	.offTimeMs        = 300000,      // 5min OFF
	.sessionDurationMs = 0,          // Chronic
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Abdominal VNS — satiety mimicry
// 50Hz = physiological meal frequency. Satiety signal emulation
// Val-Laillet 2010; Krolczyk et al.
constexpr StimMode AbdominalSatiety = {
	.frequencyHz      = 50.0f,
	.pulseWidthUs     = 500.0f,
	.currentMa        = 2.0f,
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// tVNS for food cue modulation
// Cymba conchae (NEMOS). ERP changes on food stimuli
// Frontiers Hum Neurosci 2020
constexpr StimMode FoodCueModulation = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 300.0f,
	.currentMa        = 0.6f,
	.onTimeMs         = 30000,       // 30s ON
	.offTimeMs        = 30000,       // 30s OFF
	.sessionDurationMs = 7200000,    // 2 hours
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
