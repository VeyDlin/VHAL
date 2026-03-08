#pragma once
#include "StimMode.h"


namespace StimModes::Epilepsy {

// Classic implantable VNS protocol
// Left cervical vagus, A/B fiber activation -> NTS -> LC
// Johnson & Wilson 2018; Panebianco 2016
constexpr StimMode Classic = {
	.frequencyHz      = 25.0f,
	.pulseWidthUs     = 250.0f,
	.currentMa        = 1.75f,
	.onTimeMs         = 30000,       // 30s ON
	.offTimeMs        = 300000,      // 5min OFF
	.sessionDurationMs = 0,          // Chronic/indefinite
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Optimal current protocol (Norway 2025)
// Higher current = better seizure reduction (46% with >=75% reduction at >2mA)
// NOR-current study 2025
constexpr StimMode OptimalNorway = {
	.frequencyHz      = 20.0f,
	.pulseWidthUs     = 250.0f,
	.currentMa        = 2.0f,
	.onTimeMs         = 30000,
	.offTimeMs        = 300000,
	.sessionDurationMs = 0,
	.burstMode        = false,
	.burstFreqHz      = 0,
};

// Transcutaneous auricular VNS for epilepsy
// Tragus, left ear. Pilot data, less studied than implant
// Stefan et al. 2012
constexpr StimMode AuricularTragus = {
	.frequencyHz      = 20.0f,
	.pulseWidthUs     = 300.0f,
	.currentMa        = 1.0f,        // Adjust to pain threshold
	.onTimeMs         = 3600000,     // 1 hour ON
	.offTimeMs        = 0,           // Continuous within session
	.sessionDurationMs = 3600000,    // 1h x 3 sessions/day
	.burstMode        = false,
	.burstFreqHz      = 0,
};

}
