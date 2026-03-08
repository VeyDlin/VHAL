#pragma once
#include "StimMode.h"


namespace StimModes::Depression {

// Implantable VNS for treatment-resistant depression
// NTS -> LC -> norepinephrine + serotonin release
// Pardo et al.; FDA approved
constexpr StimMode Implantable = {
	.frequencyHz      = 30.0f,
	.pulseWidthUs     = 500.0f,
	.currentMa        = 1.0f,
	.onTimeMs         = 30000,       // 30s ON
	.offTimeMs        = 300000,      // 5min OFF
	.sessionDurationMs = 0,          // Chronic
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Transcutaneous auricular VNS for depression
// Cymba conchae, left ear. Similar fMRI effects to implant
// Rong et al. 2016
constexpr StimMode Auricular = {
	.frequencyHz      = 20.0f,
	.pulseWidthUs     = 200.0f,
	.currentMa        = 0.5f,
	.onTimeMs         = 30000,       // 30s ON
	.offTimeMs        = 30000,       // 30s OFF
	.sessionDurationMs = 1800000,    // 30 min session
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
