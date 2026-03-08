#include "DemoStim.h"
#include <Adapter/Helper/TIM/ITIMHelper.h>
#include <Utilities/Math/IQMath/IQ.h>


namespace DemoStim {

// === Hardware constants ===
constexpr uint8 waveformSize = 64;
constexpr uint8 carrierClockFreq = 32;          // 32 MHz WaveGen clock
constexpr uint8 chargeBalanceNegScale = 85;     // 85/256 ≈ 1/3 (charge balance)
constexpr uint8 switchConfig = 0x01;            // Normal polarity

// === State ===
static ITIMHelper<iq16> pulseTimer;

static volatile uint8 pulseAmplitude = 0;


// === Fill all waveform points with uniform amplitude ===
static void FillWaveform(uint8 amplitude) {
	for (uint8 i = 0; i < waveformSize; i++) {
		BSP::waveGen.WriteWaveformPoint(i, amplitude);
	}
}


// === Timer ISR: trigger one biphasic pulse ===
static void OnPulseTick() {
	BSP::waveGen.Start();
}


// === Configure WaveGen carrier from mode parameters ===
static void ConfigureCarrier(const StimMode& mode) {
	pulseAmplitude = StimMode::AmplitudeFromMa(mode.currentMa);
	FillWaveform(pulseAmplitude);

	// Positive half-wave period per waveform point (µs)
	// Total positive phase = waveformSize × posHalf ≈ pulseWidthUs
	uint32 posHalf = mode.pulseWidthUs.ToInt() / waveformSize;
	if (posHalf < 1) posHalf = 1;

	// Negative half = 3× positive for charge balance (negScale ≈ 1/3)
	uint32 negHalf = posHalf * 3;

	BSP::waveGen.SetParameters(AWaveGen::Parameters{
		.config = AWaveGen::ConfigOption(AWaveGen::Config::NegativeEnable),
		// No ContinueRepeat: single biphasic pulse per Start()
		.halfWavePeriod = posHalf,
		.negHalfWavePeriod = negHalf,
		.clockFreqDiv = carrierClockFreq,
		.negScale = chargeBalanceNegScale,
		.currentSelect = AWaveGen::CurrentSelect::I0,
		.switchConfig = switchConfig,
	});
}


// === Configure timer for pulse repetition ===
static void ConfigurePulseTimer(iq16 frequencyHz) {
	BSP::timer0.EnableCounter(false);
	BSP::timer0.SetParameters({
		.mode = TIMAdapter001::Counter::Down,
	});
	pulseTimer = ITIMHelper<iq16>(BSP::timer0);
	pulseTimer.SetBaseFrequency(frequencyHz);
	BSP::timer0.onUpdateEvent = OnPulseTick;
	BSP::timer0.EnableInterrupt({ TIMAdapter001::Interrupt::Update });
	NVIC_EnableIRQ(TIMER0_IRQn);
}


// === Enable/disable stimulation output ===
static void EnableStimulation(bool enable) {
	BSP::timer0.EnableCounter(enable);
	if (!enable) {
		BSP::waveGen.Stop();
	}
}


// === Delay with session timeout check ===
// Returns false if session timed out
static bool DelayChecked(uint32 delayMs, uint32 sessionStart, uint32 sessionDurationMs) {
	uint32 start = System::GetTick();
	while ((System::GetTick() - start) < delayMs) {
		if (sessionDurationMs > 0 &&
			(System::GetTick() - sessionStart) >= sessionDurationMs) {
			return false;
		}
		System::DelayMs(100);
	}
	return true;
}


// === Main entry point ===
void Run(const StimMode& mode) {
	ConfigureCarrier(mode);
	ConfigurePulseTimer(mode.frequencyHz);

	uint32 sessionStart = System::GetTick();
	bool dutyCycled = (mode.onTimeMs > 0 && mode.offTimeMs > 0);

	EnableStimulation(true);

	while (true) {
		// Session timeout (continuous modes)
		if (!dutyCycled) {
			if (mode.sessionDurationMs > 0 &&
				(System::GetTick() - sessionStart) >= mode.sessionDurationMs) {
				break;
			}
			System::DelayMs(100);
			continue;
		}

		// ON period
		if (!DelayChecked(mode.onTimeMs, sessionStart, mode.sessionDurationMs)) {
			break;
		}

		// OFF period
		EnableStimulation(false);
		if (!DelayChecked(mode.offTimeMs, sessionStart, mode.sessionDurationMs)) {
			break;
		}

		// Re-enable for next cycle
		EnableStimulation(true);
	}

	EnableStimulation(false);
}

}
