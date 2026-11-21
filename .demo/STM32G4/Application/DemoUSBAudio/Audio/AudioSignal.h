#pragma once
#include <DemoProducts.h>
#include <VHAL.h>
#include <array>
#include <cstddef>
#include <span>


// The signal of the audio demo as pure functions: the test tone the microphone sends, the volume scaling, and the downmix of the
// loopback. The tone table and the gain table are computed by the compiler (constexpr), so the board does no floating-point work at
// run time. The host test TestAudioDemo.cpp checks them against the same values in double precision.
namespace DemoAudio {


// Samples in one period of the tone: 48 for 1 kHz at 48 kHz. The sample rate must be a whole multiple of the frequency.
constexpr std::size_t ToneSamplesPerPeriod = DemoProducts::AudioSampleRate / DemoProducts::AudioToneFrequency;
static_assert(DemoProducts::AudioSampleRate % DemoProducts::AudioToneFrequency == 0, "the tone must be a whole number of samples");
constexpr double Pi = 3.14159265358979323846;
constexpr double Ln10 = 2.30258509299404568402;
// The volume is in 1/256 dB, from 0 dB (unity) down to -60 dB. The gains are Q15: 32768 is unity.
constexpr std::size_t MaxAttenuationDecibels = 60;
constexpr int32 UnityGain = 32768;
// The volume of 0 dB, which scales nothing.
constexpr int16 UnityVolume = 0;


// The Taylor series of sin, for an angle in [-pi, pi]. It is used at compile time only. Twelve terms leave an error below 1e-11.
constexpr double SineSeries(double angle) {
	double term = angle;
	double sum = angle;
	for (int index = 1; index < 12; index++) {
		term = -term * angle * angle / static_cast<double>((2 * index) * (2 * index + 1));
		sum += term;
	}

	return sum;
}


// The Taylor series of exp, for |x| up to about 7 (the largest attenuation). Forty terms leave an error far below one Q15 step.
constexpr double ExpSeries(double x) {
	double term = 1.0;
	double sum = 1.0;
	for (int index = 1; index < 40; index++) {
		term = term * x / static_cast<double>(index);
		sum += term;
	}

	return sum;
}


// One sample of the tone at the given index into its period, rounded to the nearest LSB.
constexpr int16 ToneSampleAt(std::size_t index) {
	double angle = 2.0 * Pi * static_cast<double>(index) / static_cast<double>(ToneSamplesPerPeriod);
	if (angle > Pi) {
		angle -= 2.0 * Pi;
	}

	double value = SineSeries(angle) * static_cast<double>(DemoProducts::AudioToneAmplitude);
	if (value >= 0.0) {
		return static_cast<int16>(value + 0.5);
	}

	return static_cast<int16>(value - 0.5);
}


constexpr std::array<int16, ToneSamplesPerPeriod> MakeToneTable() {
	std::array<int16, ToneSamplesPerPeriod> table{};
	for (std::size_t index = 0; index < ToneSamplesPerPeriod; index++) {
		table[index] = ToneSampleAt(index);
	}

	return table;
}


// One period of the tone. Index it modulo ToneSamplesPerPeriod.
constexpr std::array<int16, ToneSamplesPerPeriod> ToneTable = MakeToneTable();


// The gain of an attenuation of whole decibels, in Q15, rounded to the nearest step.
constexpr int32 DecibelGain(std::size_t decibels) {
	double gain = ExpSeries(-static_cast<double>(decibels) * Ln10 / 20.0);
	return static_cast<int32>(gain * static_cast<double>(UnityGain) + 0.5);
}


constexpr std::array<int32, MaxAttenuationDecibels + 1> MakeGainTable() {
	std::array<int32, MaxAttenuationDecibels + 1> table{};
	for (std::size_t decibels = 0; decibels <= MaxAttenuationDecibels; decibels++) {
		table[decibels] = DecibelGain(decibels);
	}

	return table;
}


// Q15 gain for each whole decibel of attenuation, index 0 (0 dB) to 60 (-60 dB).
constexpr std::array<int32, MaxAttenuationDecibels + 1> VolumeGains = MakeGainTable();


// The attenuation step of a volume in 1/256 dB, rounded to the nearest whole decibel and clamped to the 0 to 60 dB range. A volume above
// 0 dB is taken as 0 dB.
constexpr std::size_t VolumeIndex(int16 volume) {
	int32 magnitude = -static_cast<int32>(volume);
	if (magnitude < 0) {
		magnitude = 0;
	}

	int32 decibels = (magnitude + 128) / 256;
	if (decibels > static_cast<int32>(MaxAttenuationDecibels)) {
		decibels = static_cast<int32>(MaxAttenuationDecibels);
	}

	return static_cast<std::size_t>(decibels);
}


// The tone at a sample index, the index counted from the start of the tone (it wraps every period).
constexpr int16 ToneSample(uint32 index) {
	return ToneTable[index % ToneSamplesPerPeriod];
}


// The tone's phase after frames more samples were written. It stays in [0, ToneSamplesPerPeriod), so a long run never grows the phase.
constexpr uint32 NextTonePhase(uint32 phase, uint32 frames) {
	return static_cast<uint32>((phase + frames) % ToneSamplesPerPeriod);
}


// The loopback threshold: a speaker sample counts as sound when its magnitude is above this many LSB. 64 LSB is about -54 dBFS. While
// Windows keeps the board's speaker open with nothing playing, it sends dither of -1, 0 and +1 (measured on Windows 11). A loopback that
// took those for sound would send the dither to the host in place of the tone. The tool's loopback tone is 0.25 of full scale, far above
// this threshold.
constexpr int32 SignalThreshold = 64;


// True when a sample is above the threshold in magnitude. A speaker chunk with no such sample is silence, which does not start the
// loopback. Dither of -1, 0 and +1 is silence, and so is a sample of exactly 64 or -64.
constexpr bool HasSignal(std::span<const int16> samples) {
	for (int16 sample : samples) {
		int32 magnitude = static_cast<int32>(sample);
		if (magnitude < 0) {
			magnitude = -magnitude;
		}

		if (magnitude > SignalThreshold) {
			return true;
		}
	}

	return false;
}


// The number of samples a speaker ring holds above keep, which the pump drops while the microphone is not taking samples. Zero when the
// ring holds no more than keep.
constexpr std::size_t SpeakerExcess(std::size_t level, std::size_t keep) {
	if (level <= keep) {
		return 0;
	}

	return level - keep;
}


// A sample scaled by a volume (1/256 dB) and muted or not. Rounded to the nearest LSB. A mute gives 0, and 0 dB gives the sample.
constexpr int16 ScaleSample(int16 sample, int16 volume, bool muted) {
	if (muted) {
		return 0;
	}

	int32 product = static_cast<int32>(sample) * VolumeGains[VolumeIndex(volume)];
	return static_cast<int16>((product + 16384) >> 15);
}


// One mono frame from a stereo frame: the mean of the two channels, rounded down (an arithmetic shift). A mono signal sent on both
// channels comes back unchanged.
constexpr int16 DownmixFrame(int16 left, int16 right) {
	return static_cast<int16>((static_cast<int32>(left) + static_cast<int32>(right)) >> 1);
}


}
