#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// What one frequency of a recording measures. The amplitude is the peak of a sine at that frequency, so a full-scale sine is 1.0.
struct ToneMeasure {
	// The amplitude at the target frequency (Goertzel).
	double amplitude = 0.0;
	// The strongest of the other frequencies the measure looked at, and its amplitude.
	double strongestOtherHz = 0.0;
	double strongestOtherAmplitude = 0.0;
	// The target over the strongest other frequency, in dB. Large when the target is the only tone in the recording.
	double ratioDb = 0.0;
	// The frequency from the zero crossings, or zero when there are fewer than two.
	double zeroCrossingHz = 0.0;
	// The RMS of the recording in dB relative to full scale. A full-scale sine is about -3 dB, and silence is the floor of -180 dB.
	double levelDbfs = 0.0;
};


// The mute and master volume of an endpoint in Windows. volumeKnown is false when the master volume could not be read, which is not an error.
struct EndpointVolume {
	bool muted = false;
	bool volumeKnown = false;
	double masterDb = 0.0;
};


// How a shared stream was opened. Raw bypasses Windows' effects on the endpoint. Refused is a stream the endpoint takes as raw, but the
// raw request was not granted, so Windows' effects may change the sound. FellBack is a raw request that was granted, but the stream could
// not be initialised raw and was opened on the Windows mix, so the effects may change the sound there too. NoRawMode is an endpoint that
// reports no raw support at all.
enum class StreamMode {
	Raw,
	Refused,
	FellBack,
	NoRawMode
};


// The step of opening a shared stream that failed, or Ready when the stream opened. The step tells whether a failed raw request can be
// retried.
enum class InitStage {
	Ready,
	Activate,
	MixFormat,
	Decode,
	Initialize
};


// The samples between two times in seconds, from the first sample at or after the start to the one before the end.
std::vector<float> Segment(const std::vector<float>& samples, std::uint32_t sampleRate, double startSeconds, double endSeconds);

// The amplitude of one frequency in the samples: the bin of a DFT of the whole recording, computed by the Goertzel recurrence.
// A recording of a whole number of cycles gives the exact amplitude of a sine at that frequency.
double GoertzelAmplitude(const std::vector<float>& samples, std::uint32_t sampleRate, double frequency);

// The frequency of the positive-going zero crossings: the crossings after the first, over the time between the first and the last.
double ZeroCrossingFrequency(const std::vector<float>& samples, std::uint32_t sampleRate);

// The RMS of the samples in dB relative to full scale, with the floor of -180 dB for silence.
double LevelDbfs(const std::vector<float>& samples);

// Measures the target frequency against the other frequencies of a fixed set, which the target is never confused with. The set is
// the frequencies of the audio below half the sample rate, leaving out the ones within 5 percent of the target.
ToneMeasure MeasureTone(const std::vector<float>& samples, std::uint32_t sampleRate, double targetHz);

// True when the target is at least minRatioDb above every other frequency of the measure.
bool IsDominant(const ToneMeasure& measure, double minRatioDb);

// True when the measured frequency is within the relative tolerance of the target (0.02 is 2 percent).
bool IsFrequency(double measuredHz, double targetHz, double relativeTolerance);

// True when the level is within the range, both ends included.
bool IsLevelBetween(double levelDbfs, double minDbfs, double maxDbfs);

// One line for the report: the frequency from the zero crossings, the ratio over the strongest other frequency, and the level.
std::string DescribeTone(const ToneMeasure& measure, double targetHz);

// A packet the microphone flagged as a discontinuity: the sample index it starts at, and whether it is the first packet the capture
// delivered after the stream was started.
struct FlaggedPacket {
	std::size_t sampleIndex = 0;
	bool firstAfterStart = false;
};


// The sample indices of the flags that count. A flag on the first packet after Start is left out as the stream's start, and
// startFlagIgnored is set. Every later flag counts.
std::vector<std::size_t> CountedDiscontinuities(const std::vector<FlaggedPacket>& flagged, bool& startFlagIgnored);

// True when a packet is the first one delivered after Start: no packet with frames has come in yet.
bool IsFirstAfterStart(bool framesDelivered);

// True when a flag is recorded for a packet that starts at the sample index. A packet with no frames can be read again on the next pass,
// so a flag at the index of the last recorded one is the same packet and is not counted twice.
bool FlagIsNew(bool haveLast, std::size_t lastIndex, std::size_t index);

// How many of the recorded discontinuities fall in the window, which is taken the way Segment takes it. A discontinuity is the sample index
// where the microphone reported a gap in its data.
std::size_t GlitchesBetween(const std::vector<std::size_t>& discontinuities, std::uint32_t sampleRate, double startSeconds, double endSeconds);

// Where the discontinuities in the window are, in milliseconds from the start of the recording, for example "at 12.5 ms, 40.0 ms". Empty when
// none is in the window.
std::string DiscontinuityPlacesText(const std::vector<std::size_t>& discontinuities, std::uint32_t sampleRate, double startSeconds, double endSeconds);

// A measurement window passes when its measure passed and no discontinuity fell in it. A gap in the data makes any measure untrusted.
bool WindowPasses(bool measuredOk, std::size_t glitches);

// The window's detail for the report: the measure, with the discontinuity count and where they are in front when there is one, so a glitch
// is never silent.
std::string WindowText(std::size_t glitches, const std::string& places, const std::string& measured);

// The suffix for a step when the recording's first packet carried a start flag that was left out. Empty when none was seen.
std::string StartFlagText(bool startFlagIgnored);

// True when a session of the speaker belongs to another application that is playing: it is active and it is not this process. The system
// sounds session counts as another application too.
bool IsForeignActiveSession(bool active, std::uint32_t processId, std::uint32_t ownProcessId);

// The line for the report when the speaker has other applications' sessions: the process name when there is one session, otherwise the count
// and every process name, so the user can see which applications hold the speaker.
std::string SpeakerInUseText(const std::vector<std::string>& sessions);

// One line for the report with the mute and the master volume of an endpoint, named by label (for example "the speaker").
std::string EndpointVolumeText(const std::string& label, const EndpointVolume& volume);

// Whether a shared stream is asked to be raw, which makes Windows skip the endpoint's effects. The endpoint's raw-processing property allows
// it only when the property is present and true. A missing or false property keeps the stream on the Windows mix.
bool RawModeAllowed(bool propertyPresent, bool propertyValue);

// One line for the report with a stream's mode: raw, and that Windows effects are bypassed; refused, and that they may change the sound;
// granted but on the Windows mix, and that they may change the sound; or no raw mode on the endpoint, and that they may change the sound.
std::string StreamModeText(StreamMode mode);

// The line for the report when the recording's stream was not in the mode the probe stream reported. Empty when they agree. Both
// modes are given, so the report shows what the recording really used and what the probe showed.
std::string ModeDifferenceText(StreamMode probe, StreamMode recorded);

// The suffix for a tone step's detail when the step failed on a stream that is not raw: Windows effects are named as a likely cause, because
// an effect such as noise suppression can take a steady tone for noise. Empty when the step passed or the stream is raw.
std::string ToneFailureCause(bool passed, StreamMode mode);

// True when a raw request was granted and the stream then failed to initialise, so the stream is opened once more on the Windows mix. A
// failure before the initialise, such as a mix format that cannot be decoded, would fail the same way again, so it is not retried.
bool RetryWithoutRaw(bool granted, InitStage failedStage);

// The error of a stream that failed on the raw request and again on the retry. The first error is kept, so the report shows both.
std::string RetryFailureText(const std::string& rawError, const std::string& retryError);


// A sine of a frequency and an amplitude, generated sample by sample with the phase carried over, so consecutive calls join without a
// click. The amplitude is a peak in -1 to 1.
class ToneGenerator {
private:
	double phaseStep = 0.0;
	double phase = 0.0;
	double amplitude = 0.0;


public:
	ToneGenerator(double frequency, std::uint32_t sampleRate, double peakAmplitude);

	std::vector<float> Next(std::size_t count);
};
