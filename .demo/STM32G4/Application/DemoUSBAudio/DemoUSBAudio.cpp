#include <DemoUSBAudio/DemoUSBAudio.h>
#include <DemoUSBAudio/Audio/AudioSignal.h>
#include <DemoUSBAudio/Descriptors/AudioDescriptors.h>
#include <DemoProducts.h>
#include <Drivers/Interface/USB/Class/Audio/AudioFunction.h>
#include <Drivers/Interface/USB/Descriptor/USBSerialNumber.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <cstddef>
#include <span>


// The result of BSP::Init() for USART1, readable over SWD. ok means the console is working.
volatile ResultStatus audioConsoleStatus __attribute__((used)) = ResultStatus::ok;


namespace DemoUSBAudio {
constexpr uint64 StatusIntervalMs = 1000;
// The loopback runs for this long after the speaker's last non-silent samples. The host sends a speaker packet every millisecond, so the
// hold covers a few missed packets. During the hold, a speaker with no samples left makes the microphone send silence, not the tone, so a
// short gap does not count as a microphone underrun. Once the hold ends, the tone returns, so a speaker that stops hands the microphone
// back to the tone within a tenth of a second.
constexpr uint64 LoopbackHoldMs = 100;
// Frames moved per chunk. A frame is one microphone sample (mono) or one speaker frame (two samples, stereo). The chunk arrays stay this size.
constexpr std::size_t FeedChunkFrames = 16;
// The most chunks one pass moves. At 48 kHz a millisecond brings 48 microphone samples and a speaker packet of 96 samples (48 stereo
// frames). Eight chunks of 16 frames move 128 microphone samples and read 256 speaker samples. The cap keeps a pass short: a pass that
// ran until the microphone ring was full could take long enough for usbDevice.Process() to miss a host packet.
constexpr std::size_t MaxChunksPerPass = 8;
// The rings, in samples. The microphone's holds four 1 ms packets of 48 samples. The speaker's holds exactly four packets of 96 samples.
// Its size is even, so a packet cut at the end of the ring still leaves whole stereo frames.
constexpr uint16 MicrophoneRingSamples = 192;
constexpr uint16 SpeakerRingSamples = 384;
// The backlog the speaker ring keeps while the microphone is not taking samples: two packets. See DiscardSpeakerExcess().
constexpr std::size_t SpeakerKeepSamples = 2 * (DemoProducts::AudioSampleRate / 1000) * 2;
using AudioFunctionType = AudioFunction<MicrophoneRingSamples, SpeakerRingSamples>;


static AudioFunctionType audioFunction(AudioUsb::Function(0));
static std::array<USBFunction*, 1> functions = { &audioFunction };
static USBDeviceAdapter::Parameters usbParameters;
static USBDevice usbDevice(BSP::usbAdapter, AudioUsb::Set, functions);


// The serial number is kept in static storage, because the device keeps its span until it is stopped.
static std::array<uint8, 2 + 4 * BSP::UniqueIdSize> serialDescriptor{};


// The phase of the tone: the index into its 48-sample period of the next sample to write, always in [0, 48). The tone continues where it
// stopped, so the loopback does not restart it.
static uint32 tonePhase = 0;
// The loopback runs until this millisecond. A non-silent speaker chunk extends it, and it expires when the speaker has been silent for
// LoopbackHoldMs.
static uint64 loopbackUntilMs = 0;
// The speaker samples the demo dropped on purpose: the backlog DiscardSpeakerExcess() drops, and the samples a pass already read when the
// microphone stopped under it. The status line shows them, so a run shows whether the discard runs and whether a stop loses samples.
static uint32 speakerDiscardedSamples = 0;
static std::array<int16, FeedChunkFrames * 2> speakerChunk{};
static std::array<int16, FeedChunkFrames> microphoneChunk{};


// Reads the unique ID and derives the serial number from it, so one board keeps its serial number after a reboot.
static ResultStatus SetIdentity() {
	std::array<uint8, BSP::UniqueIdSize> uniqueId{};
	BSP::ReadUniqueId(uniqueId);

	ResultStatus status = MakeUSBSerialNumberDescriptor(uniqueId, serialDescriptor);
	if (status != ResultStatus::ok) {
		return status;
	}

	return usbDevice.SetSerialNumber(serialDescriptor);
}


// Reads up to samples speaker samples into speakerChunk and returns how many it read. A chunk with a sample above the signal threshold
// extends the loopback hold. A chunk of silence or dither does not, so a host that streams silence does not replace the tone.
static std::size_t ReadSpeaker(std::size_t samples, uint64 nowMs) {
	std::size_t read = audioFunction.ReadSamples(std::span<int16>(speakerChunk).first(samples));
	if (DemoAudio::HasSignal(std::span<const int16>(speakerChunk).first(read))) {
		loopbackUntilMs = nowMs + LoopbackHoldMs;
	}

	return read;
}


// Fills the microphone chunk with the loopback: each speaker frame scaled by the speaker's volume and mute, averaged to mono, and scaled
// by the microphone's mute. A frame the speaker did not send is silence, which keeps the microphone's ring full during the hold.
static void FillLoopback(std::size_t frames, std::size_t pairs) {
	bool micMuted = audioFunction.IsMuted(USBAudioStream::Microphone);
	int16 speakerVolume = audioFunction.GetVolume(USBAudioStream::Speaker);
	bool speakerMuted = audioFunction.IsMuted(USBAudioStream::Speaker);
	for (std::size_t index = 0; index < frames; index++) {
		if (index < pairs) {
			int16 left = DemoAudio::ScaleSample(speakerChunk[index * 2], speakerVolume, speakerMuted);
			int16 right = DemoAudio::ScaleSample(speakerChunk[index * 2 + 1], speakerVolume, speakerMuted);
			microphoneChunk[index] = DemoAudio::ScaleSample(DemoAudio::DownmixFrame(left, right), DemoAudio::UnityVolume, micMuted);
		} else {
			microphoneChunk[index] = 0;
		}
	}
}


// Fills the microphone chunk with the tone, from the current phase. The microphone's volume and mute scale it.
static void FillTone(std::size_t frames) {
	bool micMuted = audioFunction.IsMuted(USBAudioStream::Microphone);
	int16 micVolume = audioFunction.GetVolume(USBAudioStream::Microphone);
	for (std::size_t index = 0; index < frames; index++) {
		uint32 sampleIndex = tonePhase + static_cast<uint32>(index);
		microphoneChunk[index] = DemoAudio::ScaleSample(DemoAudio::ToneSample(sampleIndex), micVolume, micMuted);
	}
}


// Drops the speaker samples above SpeakerKeepSamples. It runs while the microphone ring is full, which is when a pass cannot write and so
// does not read the speaker. The speaker still delivers a packet each millisecond, and nothing else takes its samples. From the start of a
// microphone stream until the host takes its first packet, the ring is full and the speaker's packets pile up, so the ring would refuse a
// packet. The backlog kept here is two packets, so a loopback that the microphone takes keeps its samples. Samples dropped here are not
// counted as overruns: they are dropped on purpose, and the ring never refused them.
static void DiscardSpeakerExcess() {
	std::size_t excess = DemoAudio::SpeakerExcess(audioFunction.GetReadableSamples(), SpeakerKeepSamples);
	while (excess > 0) {
		std::size_t chunk = FeedChunkFrames * 2;
		if (chunk > excess) {
			chunk = excess;
		}

		std::size_t read = audioFunction.ReadSamples(std::span<int16>(speakerChunk).first(chunk));
		if (read == 0) {
			return;
		}

		speakerDiscardedSamples += static_cast<uint32>(read);
		excess -= read;
	}
}


// One pass of the audio in the main loop, of at most MaxChunksPerPass chunks. With the microphone idle, the pass reads the speaker's ring
// until it is empty, so the ring does not fill up and drop packets. With the microphone streaming, it moves chunks into the microphone's
// ring until that ring is full: the loopback while the hold runs (silence where the speaker has no sample), the tone otherwise. The
// microphone is mono, so each sample is a frame. Every chunk reads the speaker too, so the speaker's ring is drained as the microphone
// takes samples. A full microphone ring ends the pass, and the speaker's backlog above two packets is dropped instead (see
// DiscardSpeakerExcess()). A refused write ends the pass early. The cap is what keeps usbDevice.Process() from being starved: the main loop
// calls Process() once per pass, so a pass must stay short enough that a host packet waiting for Process() is not late.
static void PumpAudio(uint64 nowMs) {
	if (!audioFunction.IsStreaming(USBAudioStream::Microphone)) {
		for (std::size_t chunk = 0; chunk < MaxChunksPerPass; chunk++) {
			if (ReadSpeaker(FeedChunkFrames * 2, nowMs) == 0) {
				return;
			}
		}

		return;
	}

	for (std::size_t chunk = 0; chunk < MaxChunksPerPass; chunk++) {
		std::size_t frames = audioFunction.GetWritableSamples();
		if (frames > FeedChunkFrames) {
			frames = FeedChunkFrames;
		}

		if (frames == 0) {
			DiscardSpeakerExcess();
			return;
		}

		std::size_t read = ReadSpeaker(frames * 2, nowMs);
		std::size_t pairs = read / 2;
		bool loopback = loopbackUntilMs > nowMs;
		if (loopback) {
			FillLoopback(frames, pairs);
		} else {
			FillTone(frames);
		}

		uint16 taken = audioFunction.WriteSamples(std::span<const int16>(microphoneChunk).first(frames));
		if (taken == 0) {
			// The microphone stopped during the pass, so the speaker samples this chunk read have no place to go. They count as dropped,
			// as the discard's samples do, so spkDrop shows the speaker samples lost this way too.
			speakerDiscardedSamples += static_cast<uint32>(read);
			return;
		}

		if (!loopback) {
			tonePhase = DemoAudio::NextTonePhase(tonePhase, taken);
		}
	}
}


static void PrintStatus() {
	const char* stateName = USBDeviceStateName(usbDevice.GetState());

	const char* micName = "idle";
	if (audioFunction.IsStreaming(USBAudioStream::Microphone)) {
		micName = "active";
	}

	const char* spkName = "idle";
	if (audioFunction.IsStreaming(USBAudioStream::Speaker)) {
		spkName = "active";
	}

	const char* loopbackName = "off";
	if (loopbackUntilMs > System::GetMs()) {
		loopbackName = "on";
	}

	USBAudioCounters counters = audioFunction.GetCounters();

	// The line goes to the console ring, which never waits for USART1. Volumes are in 1/256 dB (0 is 0 dB, -15360 is -60 dB).
	System::console
		<< "t=" << System::GetMs()
		<< "ms usb=" << stateName
		<< " mic=" << micName
		<< " spk=" << spkName
		<< " rate=" << audioFunction.GetRate(USBAudioStream::Microphone)
		<< " micVol=" << static_cast<int32>(audioFunction.GetVolume(USBAudioStream::Microphone))
		<< " micMute=" << (audioFunction.IsMuted(USBAudioStream::Microphone) ? 1 : 0)
		<< " spkVol=" << static_cast<int32>(audioFunction.GetVolume(USBAudioStream::Speaker))
		<< " spkMute=" << (audioFunction.IsMuted(USBAudioStream::Speaker) ? 1 : 0)
		<< " micPkts=" << counters.microphonePackets
		<< " micUnder=" << counters.microphoneUnderruns
		<< " micRefused=" << counters.microphoneArmRefusals
		<< " spkPkts=" << counters.speakerPackets
		<< " spkOver=" << counters.speakerOverrunSamples
		<< " spkDrop=" << speakerDiscardedSamples
		<< " loopback=" << loopbackName
		<< " usbErr=" << usbDevice.GetErrorCount()
		<< Console::endl;
}


void Run(ResultStatus consoleStatus) {
	audioConsoleStatus = consoleStatus;

	if (SetIdentity() != ResultStatus::ok) {
		System::console << "USB audio identity failed" << Console::endl;
	}

	ResultStatus status = usbDevice.Start(usbParameters);
	if (status == ResultStatus::ok) {
		System::console << "USB audio started" << Console::endl;
	} else {
		System::console << "USB start failed" << Console::endl;
	}

	uint64 nextStatusMs = System::GetMs() + StatusIntervalMs;
	while (true) {
		usbDevice.Process();
		PumpAudio(System::GetMs());

		if (System::GetMs() >= nextStatusMs) {
			nextStatusMs += StatusIntervalMs;
			PrintStatus();
		}
	}
}
}
