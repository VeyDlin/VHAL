#pragma once
#include "StimMode.h"


namespace StimModes::Rehab {

// Motor recovery after stroke
// VNS paired with movement -> motor cortex neuroplasticity
// Dawson et al.; FDA approved (Vivistim)
constexpr StimMode MotorRecovery = {
	.frequencyHz      = 30.0f,
	.pulseWidthUs     = 250.0f,
	.currentMa        = 0.8f,
	.onTimeMs         = 0,           // Paired with each movement repetition
	.offTimeMs        = 0,
	.sessionDurationMs = 1800000,    // 30 min
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
