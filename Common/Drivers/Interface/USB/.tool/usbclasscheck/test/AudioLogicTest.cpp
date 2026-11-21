#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "AudioLogic.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


bool Near(double value, double expected, double tolerance) {
	return std::fabs(value - expected) <= tolerance;
}


// A sine of the frequency and the peak amplitude, sampled at the rate, the way the board's tone is sampled. The test writes the signal
// from its definition, so the expected values do not come from the code under test.
std::vector<float> Sine(double frequency, double peak, std::uint32_t rate, std::size_t count) {
	std::vector<float> samples(count, 0.0f);
	for (std::size_t index = 0; index < count; index++) {
		double angle = 2.0 * 3.14159265358979323846 * frequency * static_cast<double>(index) / static_cast<double>(rate);
		samples[index] = static_cast<float>(peak * std::sin(angle));
	}

	return samples;
}


void CheckSegment() {
	std::vector<float> ramp(48000, 0.0f);
	for (std::size_t index = 0; index < ramp.size(); index++) {
		ramp[index] = static_cast<float>(index);
	}

	// 0.1 s is sample 4800 and 0.45 s is sample 21600, so the segment holds 16800 samples from 4800.
	std::vector<float> middle = Segment(ramp, 48000, 0.1, 0.45);
	Expect(middle.size() == 16800, "a segment of 0.35 s at 48 kHz holds 16800 samples");
	Expect(!middle.empty() && middle.front() == 4800.0f, "a segment starts at its first sample");
	Expect(!middle.empty() && middle.back() == 21599.0f, "a segment ends before its end time");

	std::vector<float> clipped = Segment(ramp, 48000, 0.9, 2.0);
	Expect(clipped.size() == 4800, "a segment past the end is cut at the end of the recording");

	Expect(Segment(ramp, 48000, 0.45, 0.1).empty(), "a segment that ends before it starts is empty");
	Expect(Segment(ramp, 48000, -1.0, 0.001).size() == 48, "a negative start is taken as zero");
}


void CheckGoertzel() {
	// 1 s of 1 kHz at a quarter of full scale: the DFT bin of 1000 Hz is the amplitude, and 440 Hz is empty.
	std::vector<float> tone = Sine(1000.0, 0.25, 48000, 48000);
	Expect(Near(GoertzelAmplitude(tone, 48000, 1000.0), 0.25, 1e-4), "a 0.25 peak sine has Goertzel amplitude 0.25 at its frequency");
	Expect(GoertzelAmplitude(tone, 48000, 440.0) < 1e-3, "a 1 kHz sine has no 440 Hz component over a whole second");
	Expect(GoertzelAmplitude(std::vector<float>(), 48000, 1000.0) == 0.0, "an empty recording has amplitude zero");
}


void CheckZeroCrossings() {
	std::vector<float> tone = Sine(1000.0, 0.25, 48000, 48000);
	Expect(Near(ZeroCrossingFrequency(tone, 48000), 1000.0, 0.5), "the zero crossings of a 1 kHz sine give 1000 Hz");
	Expect(ZeroCrossingFrequency(std::vector<float>(), 48000) == 0.0, "no samples give no frequency");
	std::vector<float> oneCrossing = {-1.0f, 1.0f};
	Expect(ZeroCrossingFrequency(oneCrossing, 48000) == 0.0, "one crossing gives no frequency");
}


void CheckLevel() {
	// The RMS of a sine of peak 0.25 is 0.25 / sqrt(2) = 0.17678, which is -15.05 dB.
	Expect(Near(LevelDbfs(Sine(1000.0, 0.25, 48000, 48000)), -15.0515, 0.01), "a 0.25 peak sine is -15.05 dBFS");
	Expect(LevelDbfs(std::vector<float>(100, 0.0f)) == -180.0, "silence is the -180 dBFS floor");
	Expect(LevelDbfs(std::vector<float>()) == -180.0, "no samples is the -180 dBFS floor");
}


void CheckMeasure() {
	// A 1 kHz tone alone: the target is far above every other frequency of the set.
	ToneMeasure alone = MeasureTone(Sine(1000.0, 0.25, 48000, 48000), 48000, 1000.0);
	Expect(IsDominant(alone, 20.0), "a 1 kHz tone alone is dominant at 1 kHz");
	Expect(alone.ratioDb > 60.0, "a 1 kHz tone alone is far above the other frequencies");

	// The same tone measured for 440 Hz: the target is absent, so it is not dominant.
	ToneMeasure wrong = MeasureTone(Sine(1000.0, 0.25, 48000, 48000), 48000, 440.0);
	Expect(!IsDominant(wrong, 20.0), "a 1 kHz tone is not dominant at 440 Hz");

	// A 1 kHz tone of 0.25 and a 440 Hz tone of 0.5: at 1 kHz the 440 Hz tone is the strongest other, at 0.5 against 0.25, so the ratio is
	// 20 log10(0.25 / 0.5) = -6.02 dB.
	std::vector<float> two = Sine(1000.0, 0.25, 48000, 48000);
	std::vector<float> lower = Sine(440.0, 0.5, 48000, 48000);
	for (std::size_t index = 0; index < two.size(); index++) {
		two[index] += lower[index];
	}

	ToneMeasure mixed = MeasureTone(two, 48000, 1000.0);
	Expect(Near(mixed.amplitude, 0.25, 1e-3), "the target amplitude is read from a mix of two tones");
	Expect(mixed.strongestOtherHz == 440.0, "the strongest other frequency is 440 Hz");
	Expect(Near(mixed.strongestOtherAmplitude, 0.5, 1e-3), "the strongest other amplitude is 0.5");
	Expect(Near(mixed.ratioDb, -6.0206, 0.01), "a 0.25 target against 0.5 is -6.02 dB");
	Expect(!IsDominant(mixed, 20.0), "a target below another tone is not dominant");

	ToneMeasure silent = MeasureTone(std::vector<float>(48000, 0.0f), 48000, 1000.0);
	Expect(!IsDominant(silent, 20.0), "silence is not dominant");
	Expect(silent.levelDbfs == -180.0, "silence measures the -180 dBFS floor");
}


void CheckVerdicts() {
	Expect(IsFrequency(1015.0, 1000.0, 0.02), "1015 Hz is within 2 percent of 1000 Hz");
	Expect(!IsFrequency(1025.0, 1000.0, 0.02), "1025 Hz is outside 2 percent of 1000 Hz");
	Expect(!IsFrequency(0.0, 1000.0, 0.02), "no zero-crossing frequency is not the target");
	Expect(IsLevelBetween(-15.0, -30.0, -6.0), "-15 dBFS is in the window -30 to -6");
	Expect(IsLevelBetween(-6.0, -30.0, -6.0), "the ends of the window count");
	Expect(!IsLevelBetween(-5.0, -30.0, -6.0), "-5 dBFS is above the window");
	Expect(!IsLevelBetween(-180.0, -30.0, -6.0), "silence is below the window");
}


void CheckDescription() {
	ToneMeasure measure = MeasureTone(Sine(1000.0, 0.25, 48000, 48000), 48000, 1000.0);
	std::string text = DescribeTone(measure, 1000.0);
	Expect(text.find("target 1000 Hz") != std::string::npos, "the description names the target");
	Expect(text.find("dBFS") != std::string::npos, "the description gives the level");
}


void CheckGenerator() {
	// 1 kHz at 48 kHz is 48 samples a cycle, so sample 6 is a quarter of the way: sin(pi/4) of the peak; sample 12 is the peak.
	ToneGenerator generator(1000.0, 48000, 0.5);
	std::vector<float> first = generator.Next(12);
	Expect(first[0] == 0.0f, "the tone starts at zero");
	Expect(Near(first[6], 0.5 * std::sqrt(0.5), 1e-5), "sample 6 of 1 kHz at 48 kHz is half of sin(pi/4)");
	std::vector<float> peak = generator.Next(1);
	Expect(Near(peak[0], 0.5, 1e-5), "sample 12 of 1 kHz at 48 kHz is the peak");

	// Two calls join into the same samples as one call.
	ToneGenerator whole(440.0, 44100, 0.25);
	ToneGenerator split(440.0, 44100, 0.25);
	std::vector<float> one = whole.Next(300);
	std::vector<float> a = split.Next(123);
	std::vector<float> b = split.Next(177);
	a.insert(a.end(), b.begin(), b.end());
	bool joined = a.size() == one.size();
	for (std::size_t index = 0; joined && index < one.size(); index++) {
		if (a[index] != one[index]) {
			joined = false;
		}
	}

	Expect(joined, "two calls of the tone give the samples of one call");
}


void CheckBoardLevel() {
	// The board's tone is 8000 LSB peak, which is 8000 / 32768 of full scale. Its RMS is the peak over sqrt(2), so the level is
	// 20 log10(8000 / sqrt(2) / 32768) = -15.26 dBFS.
	Expect(Near(LevelDbfs(Sine(1000.0, 8000.0 / 32768.0, 48000, 48000)), -15.26, 0.01), "the board's 8000 LSB peak sine is -15.26 dBFS RMS");
}


void CheckGlitches() {
	// At 48 kHz: 0.1 s is sample 4800, 0.45 s is 21600, 0.8 s is 38400, 1.0 s is 48000 and 1.4 s is 67200.
	std::vector<std::size_t> marks = {4800, 21599, 21600, 30000};
	Expect(GlitchesBetween(marks, 48000, 0.1, 0.45) == 2, "a mark at the start of a window counts, and one at its end does not");
	Expect(GlitchesBetween(marks, 48000, 0.45, 1.0) == 2, "the marks from 0.45 s to 1 s are two");
	Expect(GlitchesBetween(marks, 48000, 0.8, 1.4) == 0, "no mark falls between 0.8 s and 1.4 s");
	Expect(GlitchesBetween(marks, 48000, 0.0, 1.0) == 4, "every mark is in the whole second");
	Expect(GlitchesBetween(std::vector<std::size_t>(), 48000, 0.0, 1.0) == 0, "no discontinuities is zero glitches");
}


void CheckPlaces() {
	// At 48 kHz: 4800 is 100.0 ms, 21600 is 450.0 ms and 30000 is 625.0 ms.
	std::vector<std::size_t> marks = {4800, 21600, 30000};
	Expect(
		DiscontinuityPlacesText(marks, 48000, 0.0, 1.0) == "at 100.0 ms, 450.0 ms, 625.0 ms",
		"the places are given in ms from the start of the recording");
	Expect(
		DiscontinuityPlacesText(marks, 48000, 0.45, 1.0) == "at 450.0 ms, 625.0 ms",
		"a place at the start of a window is listed, and one at its end is not");
	Expect(DiscontinuityPlacesText(marks, 48000, 0.8, 1.4).empty(), "no place in a window gives no text");
	Expect(DiscontinuityPlacesText(std::vector<std::size_t>(), 48000, 0.0, 1.0).empty(), "no discontinuities give no places");
}


void CheckWindows() {
	Expect(WindowPasses(true, 0), "a measure that passed with no discontinuity passes");
	Expect(!WindowPasses(true, 1), "a discontinuity fails a window whose measure passed");
	Expect(!WindowPasses(false, 0), "a measure that failed fails the window");
	Expect(WindowText(0, "", "level -15.3 dBFS") == "level -15.3 dBFS; 0 discontinuities", "a clean window reports the count of zero");
	Expect(
		WindowText(1, "at 100.0 ms", "level -15.3 dBFS") == "capture glitch: 1 discontinuity at 100.0 ms; level -15.3 dBFS",
		"one discontinuity is named in the singular, with its place");
	Expect(WindowText(1, "", "x") == "capture glitch: 1 discontinuity; x", "a count without places is still named");
	Expect(
		WindowText(3, "at 1.0 ms, 2.0 ms, 3.0 ms", "x") == "capture glitch: 3 discontinuities at 1.0 ms, 2.0 ms, 3.0 ms; x",
		"several discontinuities are named in the plural, with their places");
}


FlaggedPacket Flag(std::size_t sampleIndex, bool firstAfterStart) {
	FlaggedPacket packet;
	packet.sampleIndex = sampleIndex;
	packet.firstAfterStart = firstAfterStart;
	return packet;
}


void CheckStartFlag() {
	bool ignored = false;

	// A flag on the first packet after Start is the stream's start, so it is not counted and is reported as ignored.
	std::vector<std::size_t> counted = CountedDiscontinuities({Flag(4800, true)}, ignored);
	Expect(counted.empty(), "a flag on the first packet after Start is not counted");
	Expect(ignored, "a flag on the first packet after Start is reported as ignored");

	// A flag on a later packet is a glitch, and it is counted at its sample.
	counted = CountedDiscontinuities({Flag(9600, false)}, ignored);
	Expect(counted.size() == 1 && counted[0] == 9600, "a flag on a later packet is counted at its sample");
	Expect(!ignored, "no start flag is not reported as ignored");

	// Both: the start flag is left out and the later flag counts once.
	counted = CountedDiscontinuities({Flag(0, true), Flag(9600, false)}, ignored);
	Expect(counted.size() == 1 && counted[0] == 9600, "a start flag with a later flag counts the later one once");
	Expect(ignored, "a start flag with a later flag is still reported as ignored");

	counted = CountedDiscontinuities(std::vector<FlaggedPacket>(), ignored);
	Expect(counted.empty() && !ignored, "no flags count nothing and ignore nothing");

	Expect(StartFlagText(false).empty(), "no start flag adds no text to a step");
	Expect(
		StartFlagText(true) == "; start flag seen and ignored (the first packet after Start)",
		"an ignored start flag is named in the step");
}


void CheckFirstAfterStart() {
	Expect(IsFirstAfterStart(false), "a packet before any packet with frames is the first after Start");
	Expect(!IsFirstAfterStart(true), "a packet after a packet with frames is not the first after Start");
}


void CheckFlagIsNew() {
	Expect(FlagIsNew(false, 0, 0), "the first flag of a recording is new");
	Expect(FlagIsNew(true, 4800, 9600), "a flag at a later sample is new");
	Expect(!FlagIsNew(true, 4800, 4800), "a flag at the index of the last recorded one is the same packet");
}


void CheckSessions() {
	Expect(IsForeignActiveSession(true, 1234, 99), "an active session of another process is in use");
	Expect(!IsForeignActiveSession(true, 99, 99), "an active session of this process is not another application");
	Expect(!IsForeignActiveSession(false, 1234, 99), "an inactive session is not in use");
	Expect(IsForeignActiveSession(true, 0, 99), "process 0 is not this process; the system sounds session is left out by its IsSystemSoundsSession test, not here");
	Expect(SpeakerInUseText({"chrome.exe"}) == "the board's speaker is in use by chrome.exe", "one session names its process");
	Expect(SpeakerInUseText({"a.exe", "b.exe", "a.exe"}) == "the board's speaker is in use by 3 sessions: a.exe, b.exe, a.exe", "several sessions are counted and each is named");
	Expect(SpeakerInUseText({"chrome.exe", "Discord.exe"}) == "the board's speaker is in use by 2 sessions: chrome.exe, Discord.exe", "two sessions name both processes");
}


void CheckEndpointVolume() {
	EndpointVolume volume;
	volume.volumeKnown = true;
	volume.masterDb = -12.0;
	Expect(EndpointVolumeText("the speaker", volume) == "the speaker muted: no, master volume -12.0 dB", "an unmuted speaker is named with its volume");
	volume.muted = true;
	Expect(EndpointVolumeText("the speaker", volume) == "the speaker muted: yes, master volume -12.0 dB", "a muted speaker says so");
	volume.volumeKnown = false;
	Expect(EndpointVolumeText("the microphone", volume) == "the microphone muted: yes, master volume not readable", "an unreadable master volume is said so");
}


void CheckRawMode() {
	Expect(RawModeAllowed(true, true), "a present and true raw-processing property allows a raw stream");
	Expect(!RawModeAllowed(true, false), "a present and false raw-processing property keeps the stream on the Windows mix");
	Expect(!RawModeAllowed(false, true), "a missing raw-processing property keeps the stream on the Windows mix");
	Expect(!RawModeAllowed(false, false), "no raw-processing property is not raw");
	Expect(StreamModeText(StreamMode::Raw) == "raw stream: Windows effects bypassed", "a raw stream is named as bypassing the Windows effects");
	Expect(
		StreamModeText(StreamMode::Refused) == "raw refused: Windows effects may change the sound",
		"a refused raw request is named as refused, not as a missing raw mode");
	Expect(
		StreamModeText(StreamMode::NoRawMode) == "Windows effects may change the sound: the endpoint has no raw mode",
		"an endpoint with no raw mode says the effects may change the sound");
	Expect(ModeDifferenceText(StreamMode::Raw, StreamMode::Raw).empty(), "a recording in the probe's mode adds no line");
	Expect(
		ModeDifferenceText(StreamMode::Raw, StreamMode::Refused)
			== "; the probe showed raw stream: Windows effects bypassed, the recording used raw refused: Windows effects may change the sound",
		"a recording in another mode than the probe shows both modes");
	Expect(ToneFailureCause(true, StreamMode::Refused).empty(), "a passed tone step on a refused stream names no cause");
	Expect(ToneFailureCause(false, StreamMode::Raw).empty(), "a failed tone step on a raw stream names no effects cause");
	Expect(ToneFailureCause(true, StreamMode::Raw).empty(), "a passed tone step on a raw stream names no cause");
	Expect(
		ToneFailureCause(false, StreamMode::Refused) == "; likely cause: Windows effects on the endpoint, which a stream that is not raw does not bypass",
		"a failed tone step on a refused stream names the effects as a likely cause");
	Expect(
		ToneFailureCause(false, StreamMode::NoRawMode) == "; likely cause: Windows effects on the endpoint, which a stream that is not raw does not bypass",
		"a failed tone step on an endpoint with no raw mode names the effects as a likely cause");
	Expect(
		StreamModeText(StreamMode::FellBack)
			== "raw granted, but the stream fell back to the Windows mix: Windows effects may change the sound",
		"a granted raw request that fell back to the mix is named as fallen back, not as refused");
	Expect(
		ToneFailureCause(false, StreamMode::FellBack) == "; likely cause: Windows effects on the endpoint, which a stream that is not raw does not bypass",
		"a failed tone step on a stream that fell back to the mix names the effects as a likely cause");
	Expect(
		ModeDifferenceText(StreamMode::Raw, StreamMode::FellBack)
			== "; the probe showed raw stream: Windows effects bypassed, the recording used raw granted, but the stream fell back to the Windows mix: "
				"Windows effects may change the sound",
		"a recording that fell back shows the probe's raw mode and the fallback");
}


void CheckRetry() {
	// Only a failure to initialise after a granted raw request is retried. A raw request that was refused is not retried, and neither is a
	// failure before the initialise, which would fail the same way again.
	Expect(RetryWithoutRaw(true, InitStage::Initialize), "a failed initialise after a granted raw request is retried");
	Expect(!RetryWithoutRaw(false, InitStage::Initialize), "a failed initialise after a refused raw request is not retried");
	Expect(!RetryWithoutRaw(true, InitStage::Decode), "a mix format that cannot be decoded is not retried");
	Expect(!RetryWithoutRaw(true, InitStage::MixFormat), "a mix format that cannot be read is not retried");
	Expect(!RetryWithoutRaw(true, InitStage::Activate), "a client that cannot be activated is not retried");
	Expect(!RetryWithoutRaw(true, InitStage::Ready), "a stream that opened is not retried");
	Expect(
		RetryFailureText("the shared-mode stream could not be initialised: 0x88890008", "the shared-mode stream could not be initialised: 0x88890001")
			== "the shared-mode stream could not be initialised: 0x88890008; the retry without raw failed too: "
				"the shared-mode stream could not be initialised: 0x88890001",
		"a retry that fails too keeps the first error and names the retry");
}


}


int main() {
	CheckSegment();
	CheckGoertzel();
	CheckZeroCrossings();
	CheckLevel();
	CheckMeasure();
	CheckVerdicts();
	CheckDescription();
	CheckGenerator();
	CheckBoardLevel();
	CheckGlitches();
	CheckPlaces();
	CheckWindows();
	CheckStartFlag();
	CheckFirstAfterStart();
	CheckFlagIsNew();
	CheckSessions();
	CheckEndpointVolume();
	CheckRawMode();
	CheckRetry();

	std::printf("usbclasscheck audio logic test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
