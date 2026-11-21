#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AudioLogic.h"
#include "WinAudioCom.h"

#include <mmdeviceapi.h>

// What was recorded: the mono samples at the microphone's sample rate, from the start of the recording.
struct Recording {
	std::uint32_t sampleRate = 0;
	std::vector<float> samples;
	// The sample index of each packet the microphone flagged as a discontinuity, a gap in its data, except the flag on the first packet
	// after Start, which is left out as the stream's start (see CountedDiscontinuities). A gap inside a measurement window makes the window's result
	// untrusted, so the check needs the places, not only the count.
	std::vector<std::size_t> discontinuities;
	// Whether the first packet after Start carried a flag, which was left out of discontinuities.
	bool startFlagIgnored = false;
	// The mode each stream really ran in. A tone that fails on a stream that is not raw can be the effects' doing, and the check says so.
	StreamMode microphoneMode = StreamMode::NoRawMode;
	StreamMode speakerMode = StreamMode::NoRawMode;
};


// What a recording does. It lasts recordSeconds. When play is set, the speaker plays a sine of toneHz and toneAmplitude from
// playStartSeconds after the start of the recording, for playSeconds.
struct RecordPlan {
	double recordSeconds = 0.0;
	bool play = false;
	double playStartSeconds = 0.0;
	double playSeconds = 0.0;
	double toneHz = 0.0;
	double toneAmplitude = 0.0;
};


// Records the microphone for the plan's length, and plays the plan's tone through the speaker in its window when plan.play is set. Both
// streams are shared mode at the endpoints' own formats, and both are stopped and released before the function returns. The recording
// is timed by the samples it holds, so the tone starts at the same place in it on every run.
bool RecordMicrophone(IMMDevice* microphone, IMMDevice* speaker, const RecordPlan& plan, Recording& recording, std::string& error);
