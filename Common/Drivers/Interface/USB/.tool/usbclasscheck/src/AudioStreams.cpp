#include "AudioStreams.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <audioclient.h>
#include <windows.h>

#include "AudioEndpoints.h"
#include "AudioFormat.h"
#include "AudioLogic.h"

namespace {

// How often the loop reads the microphone and fills the speaker. The shared buffer holds 200 ms, far more than one pass takes.
constexpr DWORD PollMs = 5;
// The recording may run this long beyond its length before it is given up. A device that stalls then fails the check and does not hang it.
constexpr double GraceSeconds = 3.0;


// Reads every packet the microphone has ready and appends its mono samples. A silent packet is appended as zeros. A packet the microphone
// flags as a discontinuity is noted by the sample index it starts at, and by whether it is the first packet delivered after Start;
// delivered is set once a packet with frames has come in, so the flag on the first of them can be told apart. The drain stops at the
// deadline, a GetTickCount64 value, so a microphone that keeps delivering packets cannot hold the check.
bool DrainCapture(
	IAudioCaptureClient* capture,
	const PcmFormat& format,
	ULONGLONG deadline,
	std::vector<float>& samples,
	std::vector<FlaggedPacket>& flagged,
	bool& delivered,
	std::string& error)
{
	UINT32 packet = 0;
	HRESULT result = capture->GetNextPacketSize(&packet);
	if (FAILED(result)) {
		error = HresultText("the microphone could not be read", result);
		return false;
	}

	while (packet > 0) {
		if (GetTickCount64() > deadline) {
			error = "the microphone kept delivering packets past the time allowed";
			return false;
		}

		BYTE* data = nullptr;
		UINT32 frames = 0;
		DWORD flags = 0;
		result = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
		if (FAILED(result)) {
			error = HresultText("the microphone's packet could not be read", result);
			return false;
		}

		if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
			bool haveLast = !flagged.empty();
			std::size_t lastIndex = haveLast ? flagged.back().sampleIndex : 0;
			if (FlagIsNew(haveLast, lastIndex, samples.size())) {
				FlaggedPacket marked;
				marked.sampleIndex = samples.size();
				marked.firstAfterStart = IsFirstAfterStart(delivered);
				flagged.push_back(marked);
			}
		}

		if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr) {
			samples.insert(samples.end(), static_cast<std::size_t>(frames), 0.0f);
		} else {
			std::vector<float> mono = DecodeMono(data, frames, format);
			samples.insert(samples.end(), mono.begin(), mono.end());
		}

		result = capture->ReleaseBuffer(frames);
		if (FAILED(result)) {
			error = HresultText("the microphone's packet could not be released", result);
			return false;
		}

		if (frames > 0) {
			delivered = true;
		}

		// A packet with no frames is not data. It is released above, and the drain stops here so the recording loop can feed the speaker and
		// check the deadline before it reads on. If the microphone reports the packet again, FlagIsNew keeps its flag from counting twice.
		if (frames == 0) {
			break;
		}

		result = capture->GetNextPacketSize(&packet);
		if (FAILED(result)) {
			error = HresultText("the microphone could not be read", result);
			return false;
		}
	}

	return true;
}


// Writes as much of the tone as the speaker's buffer has room for, and no more than the frames still to send.
bool FeedRender(
	IAudioClient* client,
	IAudioRenderClient* render,
	const PcmFormat& format,
	ToneGenerator& tone,
	std::uint64_t& remainingFrames,
	std::string& error)
{
	UINT32 bufferFrames = 0;
	HRESULT result = client->GetBufferSize(&bufferFrames);
	if (FAILED(result)) {
		error = HresultText("the speaker's buffer size could not be read", result);
		return false;
	}

	UINT32 padding = 0;
	result = client->GetCurrentPadding(&padding);
	if (FAILED(result)) {
		error = HresultText("the speaker's buffer could not be read", result);
		return false;
	}

	UINT32 room = padding < bufferFrames ? bufferFrames - padding : 0;
	UINT32 frames = static_cast<UINT32>(std::min<std::uint64_t>(room, remainingFrames));
	if (frames == 0) {
		return true;
	}

	BYTE* data = nullptr;
	result = render->GetBuffer(frames, &data);
	if (FAILED(result) || data == nullptr) {
		error = HresultText("the speaker's buffer could not be written", result);
		return false;
	}

	std::vector<std::uint8_t> bytes = EncodeMono(tone.Next(frames), format);
	std::memcpy(data, bytes.data(), bytes.size());
	result = render->ReleaseBuffer(frames, 0);
	if (FAILED(result)) {
		error = HresultText("the speaker's buffer could not be released", result);
		return false;
	}

	remainingFrames -= frames;
	return true;
}


std::uint64_t FramesIn(double seconds, std::uint32_t sampleRate) {
	if (seconds <= 0.0) {
		return 0;
	}

	return static_cast<std::uint64_t>(std::llround(seconds * static_cast<double>(sampleRate)));
}

}


bool RecordMicrophone(IMMDevice* microphone, IMMDevice* speaker, const RecordPlan& plan, Recording& recording, std::string& error) {
	recording.samples.clear();
	recording.discontinuities.clear();
	recording.startFlagIgnored = false;
	recording.sampleRate = 0;
	recording.microphoneMode = StreamMode::NoRawMode;
	recording.speakerMode = StreamMode::NoRawMode;
	if (plan.play && speaker == nullptr) {
		error = "the tone has no speaker to play to";
		return false;
	}

	ComPointer<IAudioClient> captureClient;
	PcmFormat captureFormat;
	if (!OpenSharedStream(microphone, captureClient, captureFormat, recording.microphoneMode, error)) {
		return false;
	}

	ComPointer<IAudioCaptureClient> capture;
	HRESULT result = captureClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(capture.Address()));
	if (FAILED(result)) {
		error = HresultText("the capture service could not be opened", result);
		return false;
	}

	ComPointer<IAudioClient> renderClient;
	ComPointer<IAudioRenderClient> render;
	PcmFormat renderFormat;
	std::optional<ToneGenerator> tone;
	std::uint64_t remainingFrames = 0;
	if (plan.play) {
		if (!OpenSharedStream(speaker, renderClient, renderFormat, recording.speakerMode, error)) {
			return false;
		}

		result = renderClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(render.Address()));
		if (FAILED(result)) {
			error = HresultText("the render service could not be opened", result);
			return false;
		}

		tone.emplace(plan.toneHz, renderFormat.sampleRate, plan.toneAmplitude);
		remainingFrames = FramesIn(plan.playSeconds, renderFormat.sampleRate);
	}

	const std::uint64_t targetFrames = FramesIn(plan.recordSeconds, captureFormat.sampleRate);
	const std::uint64_t playStartFrame = FramesIn(plan.playStartSeconds, captureFormat.sampleRate);
	std::vector<float> samples;
	samples.reserve(static_cast<std::size_t>(targetFrames));
	std::vector<FlaggedPacket> flagged;
	bool delivered = false;

	result = captureClient->Start();
	if (FAILED(result)) {
		error = HresultText("the microphone could not be started", result);
		return false;
	}

	bool renderStarted = false;
	bool ok = true;
	ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>((plan.recordSeconds + GraceSeconds) * 1000.0);
	while (static_cast<std::uint64_t>(samples.size()) < targetFrames) {
		if (GetTickCount64() > deadline) {
			error = "the microphone delivered " + std::to_string(samples.size()) + " of " + std::to_string(targetFrames) + " samples in time";
			ok = false;
			break;
		}

		if (!DrainCapture(capture.Get(), captureFormat, deadline, samples, flagged, delivered, error)) {
			ok = false;
			break;
		}

		// The speaker gets its first samples before it starts, so the stream does not begin with an underrun.
		if (plan.play && remainingFrames > 0 && static_cast<std::uint64_t>(samples.size()) >= playStartFrame) {
			if (!FeedRender(renderClient.Get(), render.Get(), renderFormat, tone.value(), remainingFrames, error)) {
				ok = false;
				break;
			}

			if (!renderStarted) {
				result = renderClient->Start();
				if (FAILED(result)) {
					error = HresultText("the speaker could not be started", result);
					ok = false;
					break;
				}

				renderStarted = true;
			}
		}

		Sleep(PollMs);
	}

	captureClient->Stop();
	if (renderStarted) {
		renderClient->Stop();
	}

	if (!ok) {
		return false;
	}

	samples.resize(static_cast<std::size_t>(targetFrames));
	recording.sampleRate = captureFormat.sampleRate;
	recording.samples = std::move(samples);
	recording.discontinuities = CountedDiscontinuities(flagged, recording.startFlagIgnored);
	return true;
}
