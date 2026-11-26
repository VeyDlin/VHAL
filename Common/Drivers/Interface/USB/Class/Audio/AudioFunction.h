#pragma once
#include <Adapter/USBDeviceAdapter.h>
#include <Drivers/Interface/USB/Class/Audio/AudioLayout.h>
#include <Drivers/Interface/USB/Class/Audio/USBAudioBuilder.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <System/System.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <Utilities/Callback/Delegate.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>


// The two streams of an audio function: the microphone (device to host, the isochronous IN endpoint) and the speaker (host to device,
// the isochronous OUT endpoint).
enum class USBAudioStream : uint8 {
	Microphone,
	Speaker
};


// What the function counts for the application. The counters are read with GetCounters(), in a critical section.
struct USBAudioCounters {
	// Microphone packets armed for the host. The function cannot see a packet the host misses, so a missed one still counts.
	uint32 microphonePackets = 0;
	// Microphone packets padded with silence, because the ring held fewer samples than the packet needs. The padding is counted before the
	// port is asked to arm, so a packet the port refuses is an underrun too when the ring ran short for it.
	uint32 microphoneUnderruns = 0;
	// Microphone packets the port refused to arm. Nothing is armed after one, so the stream stays idle until the next SET_INTERFACE
	// alternate 1. A refusal of the first arm inside a SET_INTERFACE fails that request, and the stream does not start. A refused packet is not
	// in microphonePackets.
	uint32 microphoneArmRefusals = 0;
	// Speaker packets received, including those the ring could not take in full.
	uint32 speakerPackets = 0;
	// Speaker packets the port refused to arm. As for the microphone, the stream stays idle after one, and the first arm of a SET_INTERFACE
	// fails the request.
	uint32 speakerArmRefusals = 0;
	// Speaker packets that did not hold whole frames. The bytes after their last whole frame are dropped before the ring.
	uint32 speakerPartialPackets = 0;
	// Speaker samples dropped because the ring was full. The ring keeps the oldest samples, so the newest ones of a packet are dropped.
	uint32 speakerOverrunSamples = 0;
};


// A USB Audio 1.0 function: an Audio Control interface, a microphone and a speaker, as USBAudioBuilder lays them out. The two streams
// share the function, because they share its Audio Control interface and its interface association. Its ring sizes are in samples
// (16-bit values, both channels counted): MicrophoneSamples and SpeakerSamples. A ring should hold at least one packet of its stream: 96
// samples for 48 kHz stereo. A smaller ring still works, but it pads or drops more. A ring holds whole frames, so MicrophoneSamples must be
// a multiple of the microphone's channels and SpeakerSamples of the speaker's. A layout that does not match is refused at configuration
// (invalidParameter): a frame split across two packets would play its channels out of place.
//
// Microphone. The application writes samples from thread context with WriteSamples(). When the host selects the streaming alternate
// setting, the function arms the first IN packet: rate / 1000 frames of samples from the ring, which is the amount of one
// millisecond. Each completion, when the host has taken a packet, arms the next one, so the packet is ready before the host's next frame.
// A ring that runs short is padded with silence to the full packet, so the host gets its packets at the rate it asked for, and the padding
// is counted as an underrun. The first packet is armed before the application can write (WriteSamples() returns 0 until then), so it is
// always silence. The packet armed by a SET_INTERFACE is pending, so it goes out at the host's second frame, not the first: the first frame
// is answered with a zero-length packet (USBDeviceAdapter.h, "Isochronous endpoints"). The sample order is interleaved, so the application
// writes whole frames.
//
// The microphone is synchronous: each packet holds exactly rate / 1000 frames, so its data rate is locked to the host's frames. A source
// with its own clock (an I2S or an ADC) drifts against that. A source that is slower shows as underruns, and one that is faster as samples
// WriteSamples() refuses. Asynchronous packet sizing is future work.
//
// Speaker. The endpoint is armed for an OUT packet from the selection of the alternate setting, and each completion copies the
// packet into the ring and arms the next one at once, so the next frame is accepted during the copy. The application reads the
// samples with ReadSamples(). A packet that does not fit in the ring is cut at the ring's end: its newest samples are dropped and
// counted in speakerOverrunSamples. A packet the port took with an error gives no samples (the port reports 0 bytes). A packet that ends
// inside a frame keeps its whole frames only, and the bytes after them are dropped (counted in speakerPartialPackets), so the channels
// stay in place.
//
// Restart. SET_INTERFACE alternate 1 on a stream that is already streaming restarts it: the endpoint is closed and reopened, the ring is
// cleared and the first packet is armed again, with a stop and a start notification. When the first arm is refused, the request fails and
// the stream stays off, with no start notification. A refusal later, in a completion, leaves the stream idle until the next SET_INTERFACE
// alternate 1.
//
// Requests. The feature units answer MUTE and VOLUME (GET_CUR, SET_CUR; the volume also GET_MIN, GET_MAX and GET_RES). The volume is
// -60 dB to 0 dB in 1 dB steps, in 1/256 dB units (-15360 to 0), and a SET_CUR outside that range is stalled. Only the master channel
// has controls. The endpoint answers SAMPLING_FREQ_CONTROL (GET_CUR, SET_CUR) when the stream lists more than one rate, and a SET_CUR to
// a rate the stream did not list is stalled. A GET answers the first wLength bytes of its value when the host asks for less, and the whole
// value when it asks for more, which ends the data stage short. A GET with wLength 0 is stalled, and a SET_CUR takes exactly its value's
// length. The Audio Control interface has no alternate setting: alternate 0 is accepted and any other is stalled. Every other request is
// stalled. A SET_CUR that changes the sampling frequency of a streaming path empties its ring, so samples of the old rate are not sent or played at
// the new one. A microphone packet already armed at the change is still sent once, at the old size. The counters are kept. A SET_CUR to the rate
// the stream already has changes nothing and keeps the ring. The function does not scale the samples: the application reads GetVolume() and IsMuted() and
// applies them.
//
// Events. The stream, feature and rate handlers run in interrupt context, when the change happens. They must be short and must not block
// or allocate. They are set before the device starts; SetStreamHandler() and the others return busy once the device is configured.
//
// Bus reset and deconfiguration stop both streams and empty both rings. A bus reset also returns the volume, the mute and the rate to
// their defaults (0 dB, not muted, the first rate listed), since a host sets them again after each enumeration. The counters are kept.
template<uint16 MicrophoneSamples, uint16 SpeakerSamples>
class AudioFunction : public USBFunction {
public:
	// A stream's alternate setting changed. active is true when the stream starts.
	using StreamDelegate = Callback::Delegate<void(USBAudioStream stream, bool active)>;
	// The volume or the mute of a stream changed. The volume is in 1/256 dB.
	using FeatureDelegate = Callback::Delegate<void(USBAudioStream stream, int16 volume, bool muted)>;
	// The sampling rate of a stream changed.
	using RateDelegate = Callback::Delegate<void(USBAudioStream stream, uint32 rate)>;


	// The volume range, in 1/256 dB: -60 dB to 0 dB, one step per dB. GET_RES answers VolumeResolution.
	static constexpr int16 MinVolume = -60 * 256;
	static constexpr int16 MaxVolume = 0;
	static constexpr int16 VolumeResolution = 256;
	static constexpr int16 DefaultVolume = 0;


private:
	static_assert(MicrophoneSamples >= 1 && SpeakerSamples >= 1, "each ring holds at least one sample");
	// The largest packet of either stream: 48 kHz stereo, one frame more than a millisecond (see USBAudioPacketBytes()).
	static constexpr std::size_t MaxPacketBytes = USBAudioPacketBytes(USBAudioDefaultRate, 2);
	static constexpr std::size_t MaxPacketSamples = MaxPacketBytes / 2;
	static constexpr std::size_t StreamCount = 2;
	static constexpr std::size_t ControlBytes = 3;


	struct StreamState {
		uint8 alternate = 0;
		uint32 rate = 0;
		int16 volume = DefaultVolume;
		bool muted = false;
	};


	USBAudioLayout audioLayout;
	RingBuffer<int16, MicrophoneSamples> microphoneRing;
	RingBuffer<int16, SpeakerSamples> speakerRing;
	std::array<StreamState, StreamCount> streams{};
	// Set in OnConfigured() and cleared when the device leaves the configuration. Its presence is the "configured" state the handlers
	// check.
	USBDeviceContext* deviceContext = nullptr;
	StreamDelegate streamHandler;
	FeatureDelegate featureHandler;
	RateDelegate rateHandler;
	USBAudioCounters counters;
	std::array<uint8, MaxPacketBytes> microphoneBytes{};
	std::array<int16, MaxPacketSamples> microphoneSamples{};
	std::array<uint8, MaxPacketBytes> speakerBytes{};
	std::array<int16, MaxPacketSamples> speakerSamples{};
	// The data of one control request. Only one request is in progress at a time, so the two buffers are used by one request each.
	std::array<uint8, ControlBytes> controlSend{};
	std::array<uint8, ControlBytes> controlReceive{};


public:
	// The layout of the function (USBDescriptors::Function()). Its descriptors give the formats and the endpoints.
	explicit AudioFunction(const USBFunctionLayout& functionLayout)
		: USBFunction(functionLayout),
		  audioLayout(ParseUSBAudioLayout(functionLayout.descriptors)) {
		streams[Index(USBAudioStream::Microphone)].rate = DefaultRate(audioLayout.microphone);
		streams[Index(USBAudioStream::Speaker)].rate = DefaultRate(audioLayout.speaker);
	}


	// The rings and the transfer buffers are in use while the device runs, so the function cannot be copied or moved.
	AudioFunction(const AudioFunction&) = delete;


	AudioFunction& operator=(const AudioFunction&) = delete;


	AudioFunction(AudioFunction&&) = delete;


	AudioFunction& operator=(AudioFunction&&) = delete;


	// Thread context. Queues microphone samples for the host and returns how many were taken: fewer than samples.size() when the ring
	// fills. 0 while the microphone is not streaming, so the ring holds nothing the host would not hear.
	uint16 WriteSamples(std::span<const int16> samples) {
		if (streams[Index(USBAudioStream::Microphone)].alternate == 0) {
			return 0;
		}

		return microphoneRing.Push(samples);
	}


	// Thread context. Takes speaker samples the host sent and returns how many were copied. 0 while nothing has arrived.
	uint16 ReadSamples(std::span<int16> samples) {
		return speakerRing.Pop(samples);
	}


	// Microphone samples WriteSamples() can take now.
	uint16 GetWritableSamples() const {
		return microphoneRing.GetFreeSpace();
	}


	// Speaker samples ReadSamples() can take now.
	uint16 GetReadableSamples() const {
		return speakerRing.Size();
	}


	// True while the stream's alternate setting 1 is selected.
	bool IsStreaming(USBAudioStream stream) const {
		return streams[Index(stream)].alternate == 1;
	}


	// The sampling rate of the stream, in Hz. The first rate listed at start, or the one the host set since.
	uint32 GetRate(USBAudioStream stream) const {
		return streams[Index(stream)].rate;
	}


	// The master volume of the stream's feature unit, in 1/256 dB.
	int16 GetVolume(USBAudioStream stream) const {
		return streams[Index(stream)].volume;
	}


	// The master mute of the stream's feature unit.
	bool IsMuted(USBAudioStream stream) const {
		return streams[Index(stream)].muted;
	}


	USBAudioCounters GetCounters() const {
		System::CriticalSection(true);
		USBAudioCounters copy = counters;
		System::CriticalSection(false);
		return copy;
	}


	// The handlers are refused (busy) once the device is configured, because the callables run from the device's context.
	ResultStatus SetStreamHandler(StreamDelegate value) {
		System::CriticalSection(true);
		if (deviceContext != nullptr) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		streamHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	ResultStatus SetFeatureHandler(FeatureDelegate value) {
		System::CriticalSection(true);
		if (deviceContext != nullptr) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		featureHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	ResultStatus SetRateHandler(RateDelegate value) {
		System::CriticalSection(true);
		if (deviceContext != nullptr) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		rateHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		if (!IsLayoutValid()) {
			return ResultStatus::invalidParameter;
		}

		for (StreamState& state : streams) {
			state.alternate = 0;
		}

		microphoneRing.Clear();
		speakerRing.Clear();
		deviceContext = &context;
		return ResultStatus::ok;
	}


	void OnDeconfigured() override {
		StopAll();
		deviceContext = nullptr;
	}


	void OnBusReset() override {
		StopAll();
		deviceContext = nullptr;
		ResetFeatures();
	}


	// SET_INTERFACE of one of the streams. Alternate 1 opens the stream's isochronous endpoint, and alternate 0 closes it. The Audio
	// Control interface has no alternate setting, so only its alternate 0 is accepted, as the base class accepts it.
	ResultStatus OnSetInterface(uint8 interfaceNumber, uint8 alternate, USBDeviceContext& context) override {
		if (audioLayout.found && interfaceNumber == audioLayout.controlInterface) {
			if (alternate == 0) {
				return ResultStatus::ok;
			}

			return ResultStatus::notSupported;
		}

		USBAudioStream stream = USBAudioStream::Microphone;
		if (!FindInterface(interfaceNumber, stream)) {
			return ResultStatus::notSupported;
		}

		if (alternate == 0) {
			StopStream(stream, &context);
			return ResultStatus::ok;
		}

		if (alternate != 1) {
			return ResultStatus::notSupported;
		}

		return StartStream(stream, context);
	}


	uint8 GetInterface(uint8 interfaceNumber) const override {
		USBAudioStream stream = USBAudioStream::Microphone;
		if (!FindInterface(interfaceNumber, stream)) {
			return 0;
		}

		return streams[Index(stream)].alternate;
	}


	// The class requests of the control interface (feature units) and of the isochronous endpoints (sampling frequency). A request with a
	// data stage starts it here; OnControlDataReceived() applies it when the host's bytes arrive.
	ResultStatus OnControlRequest(const USBSetupPacket& setup, USBDeviceContext& context) override {
		if (setup.type != USBRequestType::Class) {
			return ResultStatus::notSupported;
		}

		if (setup.recipient == USBRecipient::Interface) {
			return HandleFeatureRequest(setup, context);
		}

		if (setup.recipient == USBRecipient::Endpoint) {
			return HandleSamplingRequest(setup, context);
		}

		return ResultStatus::notSupported;
	}


	// The OUT data stage of a SET_CUR has arrived. The value is applied when it is in the range the function serves.
	ResultStatus OnControlDataReceived(const USBSetupPacket& setup, std::span<const uint8> data, USBDeviceContext&) override {
		if (setup.type != USBRequestType::Class) {
			return ResultStatus::notSupported;
		}

		if (setup.recipient == USBRecipient::Interface) {
			return ApplyFeature(setup, data);
		}

		if (setup.recipient == USBRecipient::Endpoint) {
			return ApplySamplingFrequency(setup, data);
		}

		return ResultStatus::notSupported;
	}


	// A completion of one of the streams' endpoints. The microphone's packet was taken, so the next one is armed. The speaker's packet is
	// copied into the ring and the next one is armed.
	void OnTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) override {
		if (audioLayout.microphone.present && address == audioLayout.microphone.address) {
			MicrophoneTransferComplete(context);
			return;
		}

		if (audioLayout.speaker.present && address == audioLayout.speaker.address) {
			SpeakerTransferComplete(byteCount, context);
		}
	}


private:
	static std::size_t Index(USBAudioStream stream) {
		return static_cast<std::size_t>(stream);
	}


	const USBAudioStreamLayout& PathOf(USBAudioStream stream) const {
		return stream == USBAudioStream::Microphone ? audioLayout.microphone : audioLayout.speaker;
	}


	static uint32 DefaultRate(const USBAudioStreamLayout& path) {
		return path.rateCount > 0 ? path.rates[0] : 0;
	}


	// The layout must describe the streams the descriptors give, and the endpoints and interfaces the function was handed.
	bool IsLayoutValid() const {
		if (!audioLayout.found) {
			return false;
		}

		std::size_t streamCount = 0;
		if (audioLayout.microphone.present) {
			if (!IsPathValid(audioLayout.microphone) || !USBEndpoint::IsIn(audioLayout.microphone.address)) {
				return false;
			}

			// The ring holds whole frames. IsPathValid() has checked the channel count, so the division is safe.
			if (MicrophoneSamples % audioLayout.microphone.channels != 0) {
				return false;
			}

			if (!OwnsEndpoint(audioLayout.microphone.address) || !OwnsInterface(audioLayout.microphone.interfaceNumber)) {
				return false;
			}

			streamCount++;
		}

		if (audioLayout.speaker.present) {
			if (!IsPathValid(audioLayout.speaker) || USBEndpoint::IsIn(audioLayout.speaker.address)) {
				return false;
			}

			// As the microphone's: the ring holds whole frames of the speaker's channels.
			if (SpeakerSamples % audioLayout.speaker.channels != 0) {
				return false;
			}

			if (!OwnsEndpoint(audioLayout.speaker.address) || !OwnsInterface(audioLayout.speaker.interfaceNumber)) {
				return false;
			}

			streamCount++;
		}

		return streamCount >= 1 && interfaceNumbers.size() == 1 + streamCount && endpointAddresses.size() == streamCount;
	}


	static bool IsPathValid(const USBAudioStreamLayout& path) {
		if (path.channels < 1 || path.channels > 2 || path.rateCount == 0) {
			return false;
		}

		if (path.packetBytes == 0 || path.packetBytes > MaxPacketBytes) {
			return false;
		}

		// Every listed rate must give a packet of at least one frame per millisecond that fits the packet buffer, and that the endpoint's
		// wMaxPacketSize takes. A rate is set only from the list (SetRate() after IsListedRate()), so this covers every packet the function
		// arms, and an arm needs no bound of its own. The microphone sends rate / 1000 frames a millisecond. The speaker is an adaptive sink,
		// and its host may send one frame more (USBAudioPacketBytes()), so the speaker's packet takes that frame too.
		bool microphone = USBEndpoint::IsIn(path.address);
		for (std::size_t index = 0; index < path.rateCount; index++) {
			uint32 rate = path.rates[index];
			if (rate < 1000 || rate / 1000 * path.channels > MaxPacketSamples) {
				return false;
			}

			uint32 frames = rate / 1000 + (microphone ? 0U : 1U);
			uint32 neededBytes = frames * path.channels * 2U;
			if (neededBytes > static_cast<uint32>(path.packetBytes)) {
				return false;
			}
		}

		return true;
	}


	bool FindInterface(uint8 interfaceNumber, USBAudioStream& stream) const {
		if (audioLayout.microphone.present && audioLayout.microphone.interfaceNumber == interfaceNumber) {
			stream = USBAudioStream::Microphone;
			return true;
		}

		if (audioLayout.speaker.present && audioLayout.speaker.interfaceNumber == interfaceNumber) {
			stream = USBAudioStream::Speaker;
			return true;
		}

		return false;
	}


	bool FindEndpoint(uint8 address, USBAudioStream& stream) const {
		if (audioLayout.microphone.present && audioLayout.microphone.address == address) {
			stream = USBAudioStream::Microphone;
			return true;
		}

		if (audioLayout.speaker.present && audioLayout.speaker.address == address) {
			stream = USBAudioStream::Speaker;
			return true;
		}

		return false;
	}


	bool FindFeatureUnit(uint8 unit, USBAudioStream& stream) const {
		if (audioLayout.microphone.present && audioLayout.microphone.featureUnit == unit) {
			stream = USBAudioStream::Microphone;
			return true;
		}

		if (audioLayout.speaker.present && audioLayout.speaker.featureUnit == unit) {
			stream = USBAudioStream::Speaker;
			return true;
		}

		return false;
	}


	// The control interface and the unit of the request, the stream it names, and the control it asks for. Each request that reaches the
	// feature units is checked by this one function, so the stall rules are the same for a SET_CUR and its GET_CUR.
	bool ResolveFeature(const USBSetupPacket& setup, USBAudioStream& stream, uint8& selector, uint16& size) const {
		if (!audioLayout.found || static_cast<uint8>(setup.index & 0xFF) != audioLayout.controlInterface) {
			return false;
		}

		if (!FindFeatureUnit(static_cast<uint8>(setup.index >> 8), stream)) {
			return false;
		}

		// Only the master channel (0) has controls.
		if ((setup.value & 0xFF) != 0) {
			return false;
		}

		selector = static_cast<uint8>(setup.value >> 8);
		const USBAudioStreamLayout& path = PathOf(stream);
		if (selector == USBAudioMuteSelector && path.muteControl) {
			size = 1;
			return true;
		}

		if (selector == USBAudioVolumeSelector && path.volumeControl) {
			size = 2;
			return true;
		}

		return false;
	}


	ResultStatus HandleFeatureRequest(const USBSetupPacket& setup, USBDeviceContext& context) {
		USBAudioStream stream = USBAudioStream::Microphone;
		uint8 selector = 0;
		uint16 size = 0;
		if (!ResolveFeature(setup, stream, selector, size)) {
			return ResultStatus::notSupported;
		}

		if (setup.request == USBAudioSetCur) {
			if (setup.direction != USBDirection::Out || setup.length != size) {
				return ResultStatus::notSupported;
			}

			return context.ControlReceiveData(std::span<uint8>(controlReceive).first(size));
		}

		if (setup.request == USBAudioGetCur) {
			if (!IsGetRequest(setup)) {
				return ResultStatus::notSupported;
			}

			const StreamState& state = streams[Index(stream)];
			if (selector == USBAudioMuteSelector) {
				controlSend[0] = state.muted ? 1 : 0;
			} else {
				PutLe16(controlSend, static_cast<uint16>(state.volume));
			}

			return context.ControlSendData(std::span<const uint8>(controlSend).first(ReplySize(setup, size)));
		}

		// The range of the volume: the minimum, the maximum and the resolution. The mute has no range.
		bool range = setup.request == USBAudioGetMin || setup.request == USBAudioGetMax || setup.request == USBAudioGetRes;
		if (!range || selector != USBAudioVolumeSelector || !IsGetRequest(setup)) {
			return ResultStatus::notSupported;
		}

		if (setup.request == USBAudioGetMin) {
			PutLe16(controlSend, static_cast<uint16>(MinVolume));
		} else if (setup.request == USBAudioGetMax) {
			PutLe16(controlSend, static_cast<uint16>(MaxVolume));
		} else {
			PutLe16(controlSend, static_cast<uint16>(VolumeResolution));
		}

		return context.ControlSendData(std::span<const uint8>(controlSend).first(ReplySize(setup, 2)));
	}


	// A GET reads from the device: an IN request with a data stage. wLength 0 has no data stage to answer, so it is stalled.
	static bool IsGetRequest(const USBSetupPacket& setup) {
		return setup.direction == USBDirection::In && setup.length != 0;
	}


	// The bytes a GET answers: the first wLength bytes of its value when the host asks for less, the whole value when it asks for more.
	// A longer data stage ends short, which the host takes as the end of it.
	static std::size_t ReplySize(const USBSetupPacket& setup, std::size_t valueSize) {
		std::size_t requested = static_cast<std::size_t>(setup.length);
		return requested < valueSize ? requested : valueSize;
	}


	ResultStatus ApplyFeature(const USBSetupPacket& setup, std::span<const uint8> data) {
		USBAudioStream stream = USBAudioStream::Microphone;
		uint8 selector = 0;
		uint16 size = 0;
		if (setup.request != USBAudioSetCur || !ResolveFeature(setup, stream, selector, size) || data.size() != size) {
			return ResultStatus::notSupported;
		}

		if (selector == USBAudioMuteSelector) {
			if (data[0] > 1) {
				return ResultStatus::notSupported;
			}

			SetMute(stream, data[0] != 0);
			return ResultStatus::ok;
		}

		int16 volume = static_cast<int16>(static_cast<uint16>(data[0] | (data[1] << 8)));
		if (volume < MinVolume || volume > MaxVolume) {
			return ResultStatus::notSupported;
		}

		SetVolume(stream, volume);
		return ResultStatus::ok;
	}


	ResultStatus HandleSamplingRequest(const USBSetupPacket& setup, USBDeviceContext& context) {
		USBAudioStream stream = USBAudioStream::Microphone;
		if (!FindEndpoint(static_cast<uint8>(setup.index & 0xFF), stream) || !PathOf(stream).rateControl) {
			return ResultStatus::notSupported;
		}

		if (setup.value != static_cast<uint16>(USBAudioSamplingFrequencySelector << 8)) {
			return ResultStatus::notSupported;
		}

		if (setup.request == USBAudioGetCur) {
			if (!IsGetRequest(setup)) {
				return ResultStatus::notSupported;
			}

			uint32 rate = streams[Index(stream)].rate;
			controlSend[0] = static_cast<uint8>(rate & 0xFF);
			controlSend[1] = static_cast<uint8>((rate >> 8) & 0xFF);
			controlSend[2] = static_cast<uint8>((rate >> 16) & 0xFF);
			return context.ControlSendData(std::span<const uint8>(controlSend).first(ReplySize(setup, 3)));
		}

		if (setup.request == USBAudioSetCur) {
			if (setup.direction != USBDirection::Out || setup.length != 3) {
				return ResultStatus::notSupported;
			}

			return context.ControlReceiveData(std::span<uint8>(controlReceive));
		}

		return ResultStatus::notSupported;
	}


	ResultStatus ApplySamplingFrequency(const USBSetupPacket& setup, std::span<const uint8> data) {
		USBAudioStream stream = USBAudioStream::Microphone;
		if (setup.request != USBAudioSetCur || data.size() != 3 || !FindEndpoint(static_cast<uint8>(setup.index & 0xFF), stream)) {
			return ResultStatus::notSupported;
		}

		uint32 rate = static_cast<uint32>(data[0]) | (static_cast<uint32>(data[1]) << 8) | (static_cast<uint32>(data[2]) << 16);
		if (!IsListedRate(PathOf(stream), rate)) {
			return ResultStatus::notSupported;
		}

		// The rate the stream already has changes nothing, so its ring keeps its samples. A new rate empties a streaming path's ring, so the
		// samples of the old rate are not sent or played at the new one. The counters are kept.
		if (streams[Index(stream)].rate == rate) {
			return ResultStatus::ok;
		}

		if (streams[Index(stream)].alternate == 1) {
			ClearRing(stream);
		}

		SetRate(stream, rate);
		return ResultStatus::ok;
	}


	static bool IsListedRate(const USBAudioStreamLayout& path, uint32 rate) {
		for (std::size_t index = 0; index < path.rateCount; index++) {
			if (path.rates[index] == rate) {
				return true;
			}
		}

		return false;
	}


	// Writes a 16-bit value little endian into the first two bytes of the buffer.
	static void PutLe16(std::array<uint8, ControlBytes>& buffer, uint16 value) {
		buffer[0] = static_cast<uint8>(value & 0xFF);
		buffer[1] = static_cast<uint8>(value >> 8);
	}


	// Changes the value and reports it when it differs. The handlers run only for a real change.
	void SetVolume(USBAudioStream stream, int16 volume) {
		StreamState& state = streams[Index(stream)];
		if (state.volume == volume) {
			return;
		}

		state.volume = volume;
		NotifyFeature(stream);
	}


	void SetMute(USBAudioStream stream, bool muted) {
		StreamState& state = streams[Index(stream)];
		if (state.muted == muted) {
			return;
		}

		state.muted = muted;
		NotifyFeature(stream);
	}


	void SetRate(USBAudioStream stream, uint32 rate) {
		StreamState& state = streams[Index(stream)];
		if (state.rate == rate) {
			return;
		}

		state.rate = rate;
		if (rateHandler) {
			rateHandler(stream, rate);
		}
	}


	void NotifyFeature(USBAudioStream stream) {
		if (featureHandler) {
			const StreamState& state = streams[Index(stream)];
			featureHandler(stream, state.volume, state.muted);
		}
	}


	// Restores the defaults of the feature units, reporting each that changes. Called on a bus reset.
	void ResetFeatures() {
		for (std::size_t index = 0; index < StreamCount; index++) {
			USBAudioStream stream = static_cast<USBAudioStream>(index);
			const USBAudioStreamLayout& path = PathOf(stream);
			if (!path.present) {
				continue;
			}

			SetVolume(stream, DefaultVolume);
			SetMute(stream, false);
			SetRate(stream, DefaultRate(path));
		}
	}


	void NotifyStream(USBAudioStream stream, bool active) {
		if (streamHandler) {
			streamHandler(stream, active);
		}
	}


	// Starts a stream. A stream that is already running is restarted: USB 2.0 has SET_INTERFACE reset the state of the endpoints, so the
	// endpoint is closed and reopened, the ring is cleared and the first packet is armed again. A refused first arm closes the endpoint
	// again and fails the request, so no start is reported and the stream stays off.
	ResultStatus StartStream(USBAudioStream stream, USBDeviceContext& context) {
		if (streams[Index(stream)].alternate == 1) {
			StopStream(stream, &context);
		}

		const USBAudioStreamLayout& path = PathOf(stream);
		USBDeviceAdapter::EndpointConfig config;
		config.address = path.address;
		config.type = USBEndpointType::Isochronous;
		config.maxPacketSize = path.packetBytes;
		config.doubleBuffer = false;
		ResultStatus status = context.OpenEndpoint(config);
		if (status != ResultStatus::ok) {
			return status;
		}

		StreamState& state = streams[Index(stream)];
		state.alternate = 1;
		ClearRing(stream);
		ResultStatus armStatus = stream == USBAudioStream::Speaker ? ArmSpeaker(context) : ArmMicrophone(context);
		if (armStatus != ResultStatus::ok) {
			static_cast<void>(context.CloseEndpoint(path.address));
			state.alternate = 0;
			ClearRing(stream);
			return armStatus;
		}

		NotifyStream(stream, true);
		return ResultStatus::ok;
	}


	// Stops a stream. With a context the endpoint is closed. A deconfiguration passes nullptr, because the core closes the endpoints after
	// the callback. A bus reset passes nullptr too, but the core has closed them before its callback (CloseAllEndpoints() runs first, see
	// USBDevice.h), so there is nothing left to close.
	void StopStream(USBAudioStream stream, USBDeviceContext* context) {
		StreamState& state = streams[Index(stream)];
		if (state.alternate == 0) {
			return;
		}

		if (context != nullptr) {
			static_cast<void>(context->CloseEndpoint(PathOf(stream).address));
		}

		state.alternate = 0;
		ClearRing(stream);
		NotifyStream(stream, false);
	}


	void StopAll() {
		StopStream(USBAudioStream::Microphone, nullptr);
		StopStream(USBAudioStream::Speaker, nullptr);
	}


	void ClearRing(USBAudioStream stream) {
		if (stream == USBAudioStream::Microphone) {
			microphoneRing.Clear();
		} else {
			speakerRing.Clear();
		}
	}


	// Arms the speaker's OUT packet. A refused call leaves nothing armed and is counted; the caller decides what the refusal means.
	ResultStatus ArmSpeaker(USBDeviceContext& context) {
		std::span<uint8> buffer = std::span<uint8>(speakerBytes).first(audioLayout.speaker.packetBytes);
		ResultStatus status = context.StartOutTransfer(audioLayout.speaker.address, buffer);
		if (status != ResultStatus::ok) {
			counters.speakerArmRefusals++;
		}

		return status;
	}


	// Frames of one millisecond of the microphone at its rate, in samples over all channels.
	std::size_t MicrophoneSamplesPerPacket() const {
		uint32 rate = streams[Index(USBAudioStream::Microphone)].rate;
		return static_cast<std::size_t>(rate / 1000 * audioLayout.microphone.channels);
	}


	// Arms the next microphone packet: rate / 1000 frames from the ring, padded with silence when the ring runs short. Called when the
	// stream starts and at each completion, so a packet is armed only when none is. The rate is a listed one, and IsPathValid() has checked
	// that every listed rate gives a packet that fits the buffer, so the packet is never empty and never too large.
	ResultStatus ArmMicrophone(USBDeviceContext& context) {
		std::size_t samples = MicrophoneSamplesPerPacket();
		uint16 taken = microphoneRing.Pop(std::span<int16>(microphoneSamples).first(samples));
		if (taken < samples) {
			for (std::size_t index = taken; index < samples; index++) {
				microphoneSamples[index] = 0;
			}

			counters.microphoneUnderruns++;
		}

		for (std::size_t index = 0; index < samples; index++) {
			uint16 value = static_cast<uint16>(microphoneSamples[index]);
			microphoneBytes[2 * index] = static_cast<uint8>(value & 0xFF);
			microphoneBytes[2 * index + 1] = static_cast<uint8>(value >> 8);
		}

		// A refused call leaves nothing armed, and the samples taken for the packet are dropped. Nothing restarts the stream by itself: the
		// port reports no completion for a frame with nothing armed, so the stream stays idle until the next SET_INTERFACE arms a packet again.
		std::span<const uint8> packet = std::span<const uint8>(microphoneBytes).first(samples * 2);
		ResultStatus status = context.StartInTransfer(audioLayout.microphone.address, packet, false);
		if (status != ResultStatus::ok) {
			counters.microphoneArmRefusals++;
			return status;
		}

		counters.microphonePackets++;
		return ResultStatus::ok;
	}


	// The host took the armed microphone packet, so the next one is armed here, before the host's next frame. A completion after the
	// stream stopped is ignored: its endpoint is closed and its packet is gone.
	void MicrophoneTransferComplete(USBDeviceContext& context) {
		if (streams[Index(USBAudioStream::Microphone)].alternate != 1) {
			return;
		}

		// A refused arm is counted by ArmMicrophone(), and the stream stays idle after it.
		static_cast<void>(ArmMicrophone(context));
	}


	// Copies a speaker packet into the ring and arms the next one. The bytes beyond the packet size are never read, so a count from the
	// port cannot run past the buffer. A completion after the stream stopped is ignored, as the microphone's is: its ring is empty.
	void SpeakerTransferComplete(uint32 byteCount, USBDeviceContext& context) {
		if (streams[Index(USBAudioStream::Speaker)].alternate != 1) {
			return;
		}

		std::size_t limit = audioLayout.speaker.packetBytes;
		std::size_t bytes = byteCount < limit ? byteCount : limit;
		// A frame is two bytes per channel. Bytes after the last whole frame would shift the channels of the packets after them, so they
		// are dropped here, before the ring.
		std::size_t frameBytes = 2U * audioLayout.speaker.channels;
		std::size_t wholeBytes = bytes - bytes % frameBytes;
		std::size_t samples = wholeBytes / 2;
		for (std::size_t index = 0; index < samples; index++) {
			speakerSamples[index] = static_cast<int16>(static_cast<uint16>(speakerBytes[2 * index] | (speakerBytes[2 * index + 1] << 8)));
		}

		uint16 accepted = speakerRing.Push(std::span<const int16>(speakerSamples).first(samples));
		counters.speakerPackets++;
		if (wholeBytes != bytes) {
			counters.speakerPartialPackets++;
		}

		counters.speakerOverrunSamples += static_cast<uint32>(samples - accepted);
		// A refused arm is counted by ArmSpeaker(), and the stream stays idle after it.
		static_cast<void>(ArmSpeaker(context));
	}
};
