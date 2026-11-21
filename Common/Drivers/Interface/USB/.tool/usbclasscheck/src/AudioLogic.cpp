#include "AudioLogic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Text.h"

namespace {

// The floor of the dB values: silence measures this, and a zero amplitude is raised to the matching value before the logarithm.
constexpr double SilenceDb = -180.0;
constexpr double TinyAmplitude = 1e-9;
// The frequencies a tone is measured against. Each one is a tone a host could confuse with the board's 1 kHz or the loopback's 440 Hz.
constexpr double OtherFrequencies[] = {220.0, 440.0, 500.0, 750.0, 1000.0, 1500.0, 2000.0, 3000.0, 4000.0, 6000.0};
// A measured frequency that is this close (relative) to the target is the target itself, and is not an other frequency.
constexpr double SameToneBand = 0.05;
constexpr double TwoPi = 6.28318530717958647692;


double ToDb(double amplitude) {
	return 20.0 * std::log10(std::max(amplitude, TinyAmplitude));
}


// The index of the first sample at or after a time in seconds. Every window of the check is cut with this, so the windows agree on their edges.
std::size_t SampleAt(double seconds, std::uint32_t sampleRate) {
	double rate = static_cast<double>(sampleRate);
	return static_cast<std::size_t>(std::llround(std::max(seconds, 0.0) * rate));
}


// The frequencies of the fixed set that are below half the sample rate and not the target.
std::vector<double> OtherFrequenciesFor(double targetHz, std::uint32_t sampleRate) {
	std::vector<double> others;
	double nyquist = static_cast<double>(sampleRate) / 2.0;
	for (double frequency : OtherFrequencies) {
		if (frequency >= nyquist) {
			continue;
		}

		if (std::abs(frequency - targetHz) <= SameToneBand * targetHz) {
			continue;
		}

		others.push_back(frequency);
	}

	return others;
}


// Whether a sample index is in the window, taken the way Segment takes it: from the first sample at or after the start to the one before the end.
bool InWindow(std::size_t index, std::uint32_t sampleRate, double startSeconds, double endSeconds) {
	return index >= SampleAt(startSeconds, sampleRate) && index < SampleAt(endSeconds, sampleRate);
}

}


std::vector<float> Segment(const std::vector<float>& samples, std::uint32_t sampleRate, double startSeconds, double endSeconds) {
	std::size_t first = SampleAt(startSeconds, sampleRate);
	std::size_t last = SampleAt(endSeconds, sampleRate);
	first = std::min(first, samples.size());
	last = std::min(last, samples.size());
	if (last <= first) {
		return std::vector<float>();
	}

	return std::vector<float>(samples.begin() + static_cast<std::ptrdiff_t>(first), samples.begin() + static_cast<std::ptrdiff_t>(last));
}


double GoertzelAmplitude(const std::vector<float>& samples, std::uint32_t sampleRate, double frequency) {
	if (samples.empty() || sampleRate == 0) {
		return 0.0;
	}

	double omega = TwoPi * frequency / static_cast<double>(sampleRate);
	double coefficient = 2.0 * std::cos(omega);
	double previous = 0.0;
	double beforePrevious = 0.0;
	for (float sample : samples) {
		double current = static_cast<double>(sample) + coefficient * previous - beforePrevious;
		beforePrevious = previous;
		previous = current;
	}

	double power = previous * previous + beforePrevious * beforePrevious - coefficient * previous * beforePrevious;
	if (power < 0.0) {
		power = 0.0;
	}

	return 2.0 * std::sqrt(power) / static_cast<double>(samples.size());
}


double ZeroCrossingFrequency(const std::vector<float>& samples, std::uint32_t sampleRate) {
	std::size_t first = 0;
	std::size_t last = 0;
	std::size_t crossings = 0;
	for (std::size_t index = 1; index < samples.size(); index++) {
		if (samples[index - 1] < 0.0f && samples[index] >= 0.0f) {
			if (crossings == 0) {
				first = index;
			}

			last = index;
			crossings++;
		}
	}

	if (crossings < 2 || last <= first) {
		return 0.0;
	}

	// Each crossing after the first ends one cycle, and the cycles fill the time from the first crossing to the last.
	double cycles = static_cast<double>(crossings - 1);
	return cycles * static_cast<double>(sampleRate) / static_cast<double>(last - first);
}


double LevelDbfs(const std::vector<float>& samples) {
	if (samples.empty()) {
		return SilenceDb;
	}

	double sum = 0.0;
	for (float sample : samples) {
		sum += static_cast<double>(sample) * static_cast<double>(sample);
	}

	double rms = std::sqrt(sum / static_cast<double>(samples.size()));
	return ToDb(rms);
}


ToneMeasure MeasureTone(const std::vector<float>& samples, std::uint32_t sampleRate, double targetHz) {
	ToneMeasure measure;
	measure.amplitude = GoertzelAmplitude(samples, sampleRate, targetHz);
	measure.zeroCrossingHz = ZeroCrossingFrequency(samples, sampleRate);
	measure.levelDbfs = LevelDbfs(samples);
	for (double frequency : OtherFrequenciesFor(targetHz, sampleRate)) {
		double amplitude = GoertzelAmplitude(samples, sampleRate, frequency);
		if (amplitude > measure.strongestOtherAmplitude) {
			measure.strongestOtherAmplitude = amplitude;
			measure.strongestOtherHz = frequency;
		}
	}

	measure.ratioDb = ToDb(measure.amplitude) - ToDb(measure.strongestOtherAmplitude);
	return measure;
}


bool IsDominant(const ToneMeasure& measure, double minRatioDb) {
	return measure.amplitude > 0.0 && measure.ratioDb >= minRatioDb;
}


bool IsFrequency(double measuredHz, double targetHz, double relativeTolerance) {
	if (measuredHz <= 0.0 || targetHz <= 0.0) {
		return false;
	}

	return std::abs(measuredHz - targetHz) <= relativeTolerance * targetHz;
}


bool IsLevelBetween(double levelDbfs, double minDbfs, double maxDbfs) {
	return levelDbfs >= minDbfs && levelDbfs <= maxDbfs;
}


std::string DescribeTone(const ToneMeasure& measure, double targetHz) {
	std::string text;
	AppendFormat(
		text,
		"target %.0f Hz: zero crossings %.1f Hz, %.1f dB above %.0f Hz (the strongest other), level %.1f dBFS",
		targetHz,
		measure.zeroCrossingHz,
		measure.ratioDb,
		measure.strongestOtherHz,
		measure.levelDbfs);
	return text;
}


std::vector<std::size_t> CountedDiscontinuities(const std::vector<FlaggedPacket>& flagged, bool& startFlagIgnored) {
	startFlagIgnored = false;
	std::vector<std::size_t> counted;
	for (const FlaggedPacket& packet : flagged) {
		// On the board, the first packet after Start carries the flag, so that flag is the start and not a gap in the data.
		if (packet.firstAfterStart) {
			startFlagIgnored = true;
			continue;
		}

		counted.push_back(packet.sampleIndex);
	}

	return counted;
}


bool IsFirstAfterStart(bool framesDelivered) {
	return !framesDelivered;
}


bool FlagIsNew(bool haveLast, std::size_t lastIndex, std::size_t index) {
	return !haveLast || lastIndex != index;
}


std::size_t GlitchesBetween(const std::vector<std::size_t>& discontinuities, std::uint32_t sampleRate, double startSeconds, double endSeconds) {
	std::size_t count = 0;
	for (std::size_t index : discontinuities) {
		if (InWindow(index, sampleRate, startSeconds, endSeconds)) {
			count++;
		}
	}

	return count;
}


std::string DiscontinuityPlacesText(const std::vector<std::size_t>& discontinuities, std::uint32_t sampleRate, double startSeconds, double endSeconds) {
	double rate = static_cast<double>(sampleRate);
	std::string list;
	for (std::size_t index : discontinuities) {
		if (!InWindow(index, sampleRate, startSeconds, endSeconds)) {
			continue;
		}

		// The index is the first sample of the flagged packet, so its time from the start of the recording is the index over the rate.
		double milliseconds = rate > 0.0 ? static_cast<double>(index) * 1000.0 / rate : 0.0;
		std::string time;
		AppendFormat(time, "%.1f ms", milliseconds);
		list += (list.empty() ? "" : ", ") + time;
	}

	return list.empty() ? std::string() : "at " + list;
}


bool WindowPasses(bool measuredOk, std::size_t glitches) {
	return measuredOk && glitches == 0;
}


std::string WindowText(std::size_t glitches, const std::string& places, const std::string& measured) {
	if (glitches == 0) {
		return measured + "; 0 discontinuities";
	}

	std::string text;
	AppendFormat(text, "capture glitch: %lu discontinuit%s", static_cast<unsigned long>(glitches), glitches == 1 ? "y" : "ies");
	std::string where = places.empty() ? std::string() : " " + places;
	return text + where + "; " + measured;
}


std::string StartFlagText(bool startFlagIgnored) {
	if (!startFlagIgnored) {
		return "";
	}

	return "; start flag seen and ignored (the first packet after Start)";
}


bool IsForeignActiveSession(bool active, std::uint32_t processId, std::uint32_t ownProcessId) {
	return active && processId != ownProcessId;
}


std::string SpeakerInUseText(const std::vector<std::string>& sessions) {
	if (sessions.size() == 1) {
		return "the board's speaker is in use by " + sessions.front();
	}

	// Every session is named, so the report shows what holds the speaker. A name may repeat when a program has two sessions.
	std::string names;
	for (const std::string& session : sessions) {
		names += (names.empty() ? "" : ", ") + session;
	}

	std::string text;
	AppendFormat(text, "the board's speaker is in use by %lu sessions: ", static_cast<unsigned long>(sessions.size()));
	return text + names;
}


std::string EndpointVolumeText(const std::string& label, const EndpointVolume& volume) {
	std::string text = label + " muted: " + (volume.muted ? "yes" : "no") + ", ";
	if (!volume.volumeKnown) {
		return text + "master volume not readable";
	}

	AppendFormat(text, "master volume %.1f dB", volume.masterDb);
	return text;
}


bool RawModeAllowed(bool propertyPresent, bool propertyValue) {
	return propertyPresent && propertyValue;
}


std::string StreamModeText(StreamMode mode) {
	if (mode == StreamMode::Raw) {
		return "raw stream: Windows effects bypassed";
	}

	if (mode == StreamMode::Refused) {
		return "raw refused: Windows effects may change the sound";
	}

	if (mode == StreamMode::FellBack) {
		return "raw granted, but the stream fell back to the Windows mix: Windows effects may change the sound";
	}

	return "Windows effects may change the sound: the endpoint has no raw mode";
}


std::string ModeDifferenceText(StreamMode probe, StreamMode recorded) {
	if (probe == recorded) {
		return "";
	}

	return "; the probe showed " + StreamModeText(probe) + ", the recording used " + StreamModeText(recorded);
}


std::string ToneFailureCause(bool passed, StreamMode mode) {
	if (passed || mode == StreamMode::Raw) {
		return "";
	}

	return "; likely cause: Windows effects on the endpoint, which a stream that is not raw does not bypass";
}


bool RetryWithoutRaw(bool granted, InitStage failedStage) {
	return granted && failedStage == InitStage::Initialize;
}


std::string RetryFailureText(const std::string& rawError, const std::string& retryError) {
	return rawError + "; the retry without raw failed too: " + retryError;
}


ToneGenerator::ToneGenerator(double frequency, std::uint32_t sampleRate, double peakAmplitude) {
	phaseStep = TwoPi * frequency / static_cast<double>(sampleRate);
	amplitude = peakAmplitude;
}


std::vector<float> ToneGenerator::Next(std::size_t count) {
	std::vector<float> samples(count, 0.0f);
	for (std::size_t index = 0; index < count; index++) {
		samples[index] = static_cast<float>(amplitude * std::sin(phase));
		phase += phaseStep;
		if (phase >= TwoPi) {
			phase -= TwoPi;
		}
	}

	return samples;
}
