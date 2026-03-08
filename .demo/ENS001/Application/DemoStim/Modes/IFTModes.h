#pragma once
#include "StimMode.h"

// NOTE: IFT (Interferential Therapy) requires two stimulation channels
// producing slightly different carrier frequencies. The "beat frequency"
// (therapeutic frequency) = |carrier1 - carrier2|.
// ENS001-A supports this via Driver A (4 channels) + Driver B (8 channels).
// ENS001-A2 has only 2 wide-range channels — dual-channel IFT possible
// but with limited electrode configurations.
//
// In this struct:
//   frequencyHz  = beat frequency (therapeutic)
//   burstFreqHz  = carrier frequency (typically 4000 Hz)
//   burstMode    = true (carrier is always present in IFT)


namespace StimModes::IFT {

// IFT for pain relief
// Beat frequency 80-120 Hz — gate control mechanism
// Carrier 4 kHz for deep tissue penetration
// Fuentes et al. 2010; Beatti et al. 2011
constexpr StimMode PainRelief = {
	.frequencyHz      = 100.0f,      // Beat frequency
	.pulseWidthUs     = 125.0f,      // 1/(2×4000) = 125 µs per phase at carrier
	.currentMa        = 30.0f,       // Comfortable tingling
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1200000,    // 20 min
	.burstMode        = true,
	.burstFreqHz      = 4000.0f,     // 4 kHz carrier
};

// IFT for muscle stimulation
// Beat frequency 10-25 Hz — motor recruitment
// Fuentes et al. 2010
constexpr StimMode MuscleStim = {
	.frequencyHz      = 20.0f,       // Beat frequency
	.pulseWidthUs     = 125.0f,
	.currentMa        = 35.0f,       // Motor threshold
	.onTimeMs         = 10000,       // 10s ON
	.offTimeMs        = 10000,       // 10s OFF
	.sessionDurationMs = 1200000,    // 20 min
	.burstMode        = true,
	.burstFreqHz      = 4000.0f,
};

// IFT for edema / circulation
// Beat frequency 1-10 Hz — smooth muscle / vascular effect
// Fuentes et al. 2010
constexpr StimMode Circulation = {
	.frequencyHz      = 5.0f,        // Beat frequency
	.pulseWidthUs     = 125.0f,
	.currentMa        = 20.0f,       // Sub-motor threshold
	.onTimeMs         = 0,           // Continuous
	.offTimeMs        = 0,
	.sessionDurationMs = 1200000,    // 20 min
	.burstMode        = true,
	.burstFreqHz      = 4000.0f,
};

}
