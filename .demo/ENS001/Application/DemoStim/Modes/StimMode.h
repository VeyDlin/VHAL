#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Math/IQMath/IQ.h>


struct StimMode {
	// Stimulation pulse
	iq16 frequencyHz;           // Pulse repetition rate (Hz)
	iq16 pulseWidthUs;          // Pulse width per phase (µs)
	iq16 currentMa;             // Target peak current (mA)

	// Macro duty cycle (ON/OFF periods)
	uint32 onTimeMs;            // Stimulation ON time (ms), 0 = continuous
	uint32 offTimeMs;           // Rest OFF time (ms), 0 = continuous

	// Session
	uint32 sessionDurationMs;   // Total session length (ms), 0 = indefinite

	// Burst mode (e.g. gammaCore-style or IFT carrier)
	bool burstMode;             // true = kHz burst/carrier within each pulse
	iq16 burstFreqHz;           // Burst carrier frequency (Hz), 0 if !burstMode


	// --- Hardware mapping helpers ---

	// ISEL current per unit (µA) for each current select setting
	// I0=33, I1=66, I2=100, I3=133, I4=166, I5=200, I6=233, I7=266
	static constexpr iq16 CurrentSelectUnitUa(uint8 isel) {
		return iq16(33.0f * (isel + 1));
	}

	// Convert target mA to waveform amplitude (0-255) for given ISEL
	static constexpr uint8 AmplitudeFromMa(iq16 ma, uint8 isel = 0) {
		iq16 unitUa = CurrentSelectUnitUa(isel);
		iq16 amp = (ma * iq16(1000)) / unitUa;
		return static_cast<uint8>(amp > iq16(255) ? 255 : amp.ToInt());
	}
};
