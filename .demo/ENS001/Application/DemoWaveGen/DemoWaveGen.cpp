#include "DemoWaveGen.h"
#include <Utilities/Math/IQMath/IQTrig.h>


namespace DemoWaveGen {

static constexpr uint8 waveformSize = 64;

static void LoadSineWaveform() {
	constexpr iq16 twoPi = 6.2831853f;
	for (uint8 i = 0; i < waveformSize; i++) {
		iq16 angle = twoPi * i / static_cast<int>(waveformSize);
		uint8 value = static_cast<uint8>((sin(angle) + iq16(1)) * 127);
		BSP::waveGen.WriteWaveformPoint(i, value);
	}
}


void Run() {
	LoadSineWaveform();

	BSP::waveGen.SetParameters({
		.config = WaveGeneratorAdapter001::Config::ContinueRepeat,
		.halfWavePeriod = 1000,
		.negHalfWavePeriod = 1000,
		.clockFreqDiv = 32,
		.currentSelect = WaveGeneratorAdapter001::CurrentSelect::I3
	});

	// Cycle: 2s stimulation, 1s pause
	while (true) {
		BSP::waveGen.Start();
		System::DelayMs(2000);
		
		BSP::waveGen.Stop();
		System::DelayMs(1000);
	}
}

}
