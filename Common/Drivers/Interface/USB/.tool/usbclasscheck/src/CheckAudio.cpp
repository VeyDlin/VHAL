#include "CheckAudio.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <mmdeviceapi.h>
#include <windows.h>

#include "AudioEndpoints.h"
#include "AudioFormat.h"
#include "AudioLogic.h"
#include "AudioStreams.h"
#include "DemoProducts.h"
#include "Report.h"
#include "Text.h"
#include "WinAudioCom.h"

namespace {

// The microphone's tone is checked against the level the board sends: 8000 LSB peak is -15.26 dBFS as an RMS (20*log10(8000/sqrt(2)/32768)).
// The window is wide so the check allows for the volume the host has set on the microphone; it still rules out silence and clipping.
constexpr double ToneMinDbfs = -30.0;
constexpr double ToneMaxDbfs = -6.0;
// A frequency counts as the tone when it is this many dB above every other frequency of the measure.
constexpr double DominanceDb = 20.0;
// The zero-crossing frequency is within this relative tolerance of the target (0.02 is 2 percent).
constexpr double FrequencyTolerance = 0.02;

// The loopback test tone, played into the speaker. It is not in DemoProducts, because the board does not need it, only this check.
// Its peak is a quarter of full scale, so the speaker's level is well inside the range.
constexpr double LoopbackHz = 440.0;
constexpr double LoopbackPeak = 0.25;
// The loopback must reach the microphone above this level, so a silent speaker (volume at zero) is reported as a failure.
constexpr double LoopbackMinDbfs = -60.0;

// The recording lengths and the tone's place in them, in seconds. The loopback recording plays the tone from 0.5 s to 1.5 s, and
// it is measured before the speaker plays (0.1 s to 0.45 s, the board's tone) and after the switch has settled (0.8 s to 1.4 s).
constexpr double ToneRecordSeconds = 1.0;
constexpr double LoopbackRecordSeconds = 1.6;
constexpr double LoopbackPlayStartSeconds = 0.5;
constexpr double LoopbackPlaySeconds = 1.0;
constexpr double BeforeStartSeconds = 0.1;
constexpr double BeforeEndSeconds = 0.45;
constexpr double DuringStartSeconds = 0.8;
constexpr double DuringEndSeconds = 1.4;

// Windows lists a board's audio endpoints some seconds after the board enumerates, so a lookup that finds none is repeated for this long.
constexpr DWORD EndpointWaitMs = 15000;
constexpr DWORD EndpointPollMs = 500;


// Looks for the board's endpoint of the flow. While there is none, the lookup is repeated for up to EndpointWaitMs, and one line tells the
// user the check is waiting. The other outcomes (several endpoints, a failure) are returned at once.
EndpointLookup WaitForBoardEndpoint(
	const BoardInfo& board,
	EDataFlow flow,
	const std::string& label,
	BoardEndpoint& endpoint,
	std::string& problem)
{
	EndpointLookup lookup = FindBoardEndpoint(board, flow, endpoint, problem);
	if (lookup != EndpointLookup::None) {
		return lookup;
	}

	std::fprintf(
		stderr,
		"usbclasscheck: waiting up to %lu s for the board's %s endpoint\n",
		static_cast<unsigned long>(EndpointWaitMs / 1000),
		label.c_str());
	ULONGLONG deadline = GetTickCount64() + EndpointWaitMs;
	while (lookup == EndpointLookup::None && GetTickCount64() < deadline) {
		Sleep(EndpointPollMs);
		lookup = FindBoardEndpoint(board, flow, endpoint, problem);
	}

	return lookup;
}


// Opens the endpoint's stream once, to read its format, and adds a step with its name, format and mode. mode gets the mode the probe ran in.
// False when the stream cannot be opened.
bool DescribeEndpoint(const std::string& stepName, const BoardEndpoint& endpoint, CheckReport& report, StreamMode& mode) {
	ComPointer<IAudioClient> client;
	PcmFormat format;
	std::string error;
	if (!OpenSharedStream(endpoint.device.Get(), client, format, mode, error)) {
		report.AddStep(stepName, StepStatus::Error, error);
		return false;
	}

	report.AddStep(stepName, StepStatus::Pass, "\"" + endpoint.name + "\", " + FormatText(format) + "; " + StreamModeText(mode));
	return true;
}


// The verdict of a tone measure: the target is dominant and the zero crossings give its frequency.
bool ToneIsPresent(const ToneMeasure& measure, double targetHz) {
	return IsDominant(measure, DominanceDb) && IsFrequency(measure.zeroCrossingHz, targetHz, FrequencyTolerance);
}


std::string LevelText(double levelDbfs) {
	std::string text;
	AppendFormat(text, "level %.1f dBFS, the window is %.0f to %.0f dBFS", levelDbfs, ToneMinDbfs, ToneMaxDbfs);
	return text;
}


// The detail of one microphone window: the discontinuities in it and where they are, the measure, the start flag when one was left out, and
// the mode when the recording ran in another mode than the probe did.
std::string WindowDetail(
	const Recording& recording,
	double startSeconds,
	double endSeconds,
	const std::string& measured,
	StreamMode probeMode)
{
	std::size_t glitches = GlitchesBetween(recording.discontinuities, recording.sampleRate, startSeconds, endSeconds);
	std::string places = DiscontinuityPlacesText(recording.discontinuities, recording.sampleRate, startSeconds, endSeconds);
	return WindowText(glitches, places, measured) + StartFlagText(recording.startFlagIgnored) + ModeDifferenceText(probeMode, recording.microphoneMode);
}


// Reads the microphone's mute. A muted microphone records silence, so the tone cannot be checked and the check stops, as it does for a
// muted speaker. False when the check must stop.
bool CheckMicrophoneMute(const BoardEndpoint& microphone, CheckReport& report) {
	EndpointVolume volume;
	std::string error;
	if (!ReadEndpointVolume(microphone.device.Get(), volume, error)) {
		report.AddStep("microphone mute", StepStatus::Error, error);
		return false;
	}

	if (volume.muted) {
		report.AddStep(
			"microphone mute",
			StepStatus::Error,
			"the microphone is muted in Windows, so it records silence; unmute it and run the check again");
		return false;
	}

	report.AddStep("microphone mute", StepStatus::Pass, EndpointVolumeText("the microphone", volume));
	return true;
}


// Looks for other applications' active sessions on the speaker. Their sound would mix into the loopback, so its result would not be the
// board's alone. Returns true when another application is using the speaker, and the step names it. A session that cannot be read is an
// error step, and the loopback is still tried.
bool CheckSpeakerInUse(const BoardEndpoint& speaker, CheckReport& report) {
	std::vector<std::string> sessions;
	std::string error;
	if (!ActiveSpeakerSessions(speaker.device.Get(), sessions, error)) {
		report.AddStep("speaker in use", StepStatus::Error, error);
		return false;
	}

	if (sessions.empty()) {
		report.AddStep("speaker in use", StepStatus::Pass, "no other application has an active session on the speaker");
		return false;
	}

	report.AddStep("speaker in use", StepStatus::Error, SpeakerInUseText(sessions));
	return true;
}


// Records the microphone with the board's tone and checks the tone's frequency and level. microphoneProbe is the mode the microphone's probe
// stream reported. False when the recording could not be made.
bool CheckTone(const BoardEndpoint& microphone, StreamMode microphoneProbe, CheckReport& report) {
	RecordPlan plan;
	plan.recordSeconds = ToneRecordSeconds;
	Recording recording;
	std::string error;
	if (!RecordMicrophone(microphone.device.Get(), nullptr, plan, recording, error)) {
		report.AddStep("tone recording", StepStatus::Error, error);
		return false;
	}

	// The whole recording is the tone's window, so a gap anywhere in it fails both tone steps.
	std::size_t glitches = GlitchesBetween(recording.discontinuities, recording.sampleRate, 0.0, ToneRecordSeconds);
	double toneHz = static_cast<double>(DemoProducts::AudioToneFrequency);
	ToneMeasure measure = MeasureTone(recording.samples, recording.sampleRate, toneHz);
	bool frequencyOk = WindowPasses(ToneIsPresent(measure, toneHz), glitches);
	report.AddStep(
		"tone frequency",
		frequencyOk ? StepStatus::Pass : StepStatus::Fail,
		WindowDetail(recording, 0.0, ToneRecordSeconds, DescribeTone(measure, toneHz), microphoneProbe)
			+ ToneFailureCause(frequencyOk, recording.microphoneMode));

	bool levelOk = WindowPasses(IsLevelBetween(measure.levelDbfs, ToneMinDbfs, ToneMaxDbfs), glitches);
	report.AddStep(
		"tone level",
		levelOk ? StepStatus::Pass : StepStatus::Fail,
		WindowDetail(recording, 0.0, ToneRecordSeconds, LevelText(measure.levelDbfs), microphoneProbe)
			+ ToneFailureCause(levelOk, recording.microphoneMode));
	return true;
}


// Plays the loopback tone into the speaker while recording the microphone, and checks the microphone's sound before and after.
// microphoneProbe and speakerProbe are the modes the probe streams of the two endpoints reported.
void CheckLoopback(
	const BoardEndpoint& microphone,
	const BoardEndpoint& speaker,
	StreamMode microphoneProbe,
	StreamMode speakerProbe,
	CheckReport& report)
{
	EndpointVolume speakerVolume;
	std::string error;
	if (!ReadEndpointVolume(speaker.device.Get(), speakerVolume, error)) {
		report.AddStep("speaker mute", StepStatus::Error, error);
		return;
	}

	if (speakerVolume.muted) {
		report.AddStep(
			"speaker mute",
			StepStatus::Error,
			"the speaker is muted in Windows, so no sound reaches the board; unmute it and run the check again");
		return;
	}

	RecordPlan plan;
	plan.recordSeconds = LoopbackRecordSeconds;
	plan.play = true;
	plan.playStartSeconds = LoopbackPlayStartSeconds;
	plan.playSeconds = LoopbackPlaySeconds;
	plan.toneHz = LoopbackHz;
	plan.toneAmplitude = LoopbackPeak;
	Recording recording;
	if (!RecordMicrophone(microphone.device.Get(), speaker.device.Get(), plan, recording, error)) {
		report.AddStep("loopback recording", StepStatus::Error, error);
		return;
	}

	double boardToneHz = static_cast<double>(DemoProducts::AudioToneFrequency);
	std::size_t beforeGlitches = GlitchesBetween(recording.discontinuities, recording.sampleRate, BeforeStartSeconds, BeforeEndSeconds);
	std::vector<float> before = Segment(recording.samples, recording.sampleRate, BeforeStartSeconds, BeforeEndSeconds);
	ToneMeasure beforeMeasure = MeasureTone(before, recording.sampleRate, boardToneHz);
	bool beforeOk = WindowPasses(ToneIsPresent(beforeMeasure, boardToneHz), beforeGlitches);
	report.AddStep(
		"tone before playback",
		beforeOk ? StepStatus::Pass : StepStatus::Fail,
		WindowDetail(recording, BeforeStartSeconds, BeforeEndSeconds, DescribeTone(beforeMeasure, boardToneHz), microphoneProbe)
			+ ToneFailureCause(beforeOk, recording.microphoneMode));

	std::size_t duringGlitches = GlitchesBetween(recording.discontinuities, recording.sampleRate, DuringStartSeconds, DuringEndSeconds);
	std::vector<float> during = Segment(recording.samples, recording.sampleRate, DuringStartSeconds, DuringEndSeconds);
	ToneMeasure duringMeasure = MeasureTone(during, recording.sampleRate, LoopbackHz);
	bool switched = ToneIsPresent(duringMeasure, LoopbackHz) && IsLevelBetween(duringMeasure.levelDbfs, LoopbackMinDbfs, 0.0);
	bool loopbackOk = WindowPasses(switched, duringGlitches);
	std::string detail = WindowDetail(recording, DuringStartSeconds, DuringEndSeconds, DescribeTone(duringMeasure, LoopbackHz), microphoneProbe);
	// The loopback plays through the speaker, so the speaker's stream mode is part of what the step measured, and a speaker that ran in
	// another mode than its probe is named.
	detail += "; speaker: " + StreamModeText(recording.speakerMode) + ModeDifferenceText(speakerProbe, recording.speakerMode);
	detail += ToneFailureCause(loopbackOk, recording.microphoneMode);
	// The speaker's master volume is what tells a silent speaker (volume at zero) from a microphone that did not hear it.
	if (!loopbackOk) {
		detail += "; " + EndpointVolumeText("the speaker", speakerVolume);
	}

	report.AddStep("loopback", loopbackOk ? StepStatus::Pass : StepStatus::Fail, detail);
}

}


void RunAudioCheck(const BoardInfo& board, CheckReport& report) {
	// The board's USB device node is found through its serial number, so a board without one cannot be told from another. The endpoints
	// are then matched to that node by container ID.
	if (board.serial.empty()) {
		report.AddStep(
			"board",
			StepStatus::Error,
			"the board has no serial number, so its audio endpoints cannot be told from another board's");
		return;
	}

	ScopedCom com;
	if (!com.IsReady()) {
		report.AddStep("COM", StepStatus::Error, "COM could not be initialised for the audio endpoints");
		return;
	}

	BoardEndpoint microphone;
	std::string problem;
	if (WaitForBoardEndpoint(board, eCapture, "microphone", microphone, problem) != EndpointLookup::Found) {
		report.AddStep("microphone", StepStatus::Error, problem);
		return;
	}

	StreamMode microphoneProbe = StreamMode::NoRawMode;
	if (!DescribeEndpoint("microphone", microphone, report, microphoneProbe)) {
		return;
	}

	if (!CheckMicrophoneMute(microphone, report)) {
		return;
	}

	// The tone and the loopback need the microphone, but a missing speaker only stops the loopback.
	BoardEndpoint speaker;
	StreamMode speakerProbe = StreamMode::NoRawMode;
	bool speakerFound = WaitForBoardEndpoint(board, eRender, "speaker", speaker, problem) == EndpointLookup::Found;
	if (!speakerFound) {
		report.AddStep("speaker", StepStatus::Error, problem);
	} else {
		speakerFound = DescribeEndpoint("speaker", speaker, report, speakerProbe);
	}

	// Another application on the speaker is found before the tone steps, and it is reported as the loopback's error, not as a tone failure.
	bool speakerBusy = speakerFound && CheckSpeakerInUse(speaker, report);

	if (!CheckTone(microphone, microphoneProbe, report)) {
		return;
	}

	if (!speakerFound) {
		report.AddStep("loopback", StepStatus::Error, "the speaker is not available, so the loopback cannot be checked");
		return;
	}

	if (speakerBusy) {
		report.AddStep("loopback", StepStatus::Error, "another application is using the speaker, so the loopback cannot be checked");
		return;
	}

	CheckLoopback(microphone, speaker, microphoneProbe, speakerProbe, report);
}
