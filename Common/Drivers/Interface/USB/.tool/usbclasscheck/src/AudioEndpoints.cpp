#include "AudioEndpoints.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmreg.h>
#include <propidl.h>
#include <propsys.h>

#include "AudioLogic.h"
#include "DeviceTree.h"
#include "WinText.h"

namespace {

// The friendly name of an endpoint, as the sound settings show it (PKEY_Device_FriendlyName, functiondiscoverykeys.h). It is written out
// here, so this file needs no INITGUID definition of the key.
const PROPERTYKEY FriendlyNameKey = {
	{ 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } },
	14
};

// The container ID of the endpoint's device group, the same key DeviceTree.cpp reads on the USB device node.
const PROPERTYKEY ContainerIdKey = {
	{ 0x8C7ED206, 0x3F8A, 0x4827, { 0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C } },
	2
};

// Whether the endpoint takes a raw stream (PKEY_Devices_AudioDevice_RawProcessingSupported, a VT_BOOL). Windows Voice Clarity on a board's
// microphone treats a steady tone as noise, and a raw stream is the only way to read the microphone without that effect.
const PROPERTYKEY RawProcessingSupportedKey = {
	{ 0x8943B373, 0x388C, 0x4395, { 0xB5, 0x57, 0xBC, 0x6D, 0xBA, 0xFF, 0xAF, 0xDB } },
	2
};

// The shared-mode buffer, in 100-nanosecond units. It is long enough for the polling loop, which wakes every few milliseconds.
constexpr REFERENCE_TIME SharedBufferHns = 2000000;


// The layout of a mix format. Only PCM and IEEE float are read, and a block that does not add up is refused.
bool PcmFormatOf(const WAVEFORMATEX* wave, PcmFormat& format, std::string& error) {
	format.sampleRate = wave->nSamplesPerSec;
	format.channels = wave->nChannels;
	format.bitsPerSample = wave->wBitsPerSample;
	format.isFloat = false;
	if (wave->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
		format.isFloat = true;
	} else if (wave->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		if (wave->cbSize < 22) {
			error = "the mix format is an extensible format with no sub-format";
			return false;
		}

		const WAVEFORMATEXTENSIBLE* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wave);
		if (extensible->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT) {
			format.isFloat = true;
		} else if (extensible->SubFormat.Data1 != WAVE_FORMAT_PCM) {
			error = "the mix format's sub-format is neither PCM nor float";
			return false;
		}
	} else if (wave->wFormatTag != WAVE_FORMAT_PCM) {
		error = "the mix format's tag is not PCM, float or extensible";
		return false;
	}

	if (static_cast<std::size_t>(wave->nBlockAlign) != BytesPerFrame(format)) {
		error = "the mix format's block align does not match its channels and sample size";
		return false;
	}

	std::string reason = UnsupportedFormatReason(format);
	if (!reason.empty()) {
		error = "the mix format cannot be decoded: " + reason;
		return false;
	}

	return true;
}


// The container ID of an endpoint as GUID text, or empty when the endpoint has none. The property is a VT_CLSID value.
std::string ContainerIdOfEndpoint(IMMDevice* device) {
	std::string containerId;
	ComPointer<IPropertyStore> store;
	if (FAILED(device->OpenPropertyStore(STGM_READ, store.Address()))) {
		return containerId;
	}

	PROPVARIANT value;
	PropVariantInit(&value);
	if (SUCCEEDED(store->GetValue(ContainerIdKey, &value)) && value.vt == VT_CLSID && value.puuid != nullptr) {
		containerId = GuidText(*value.puuid);
	}

	PropVariantClear(&value);
	return containerId;
}


std::string FriendlyNameOf(IMMDevice* device) {
	std::string name = "(name not reported)";
	ComPointer<IPropertyStore> store;
	if (FAILED(device->OpenPropertyStore(STGM_READ, store.Address()))) {
		return name;
	}

	PROPVARIANT value;
	PropVariantInit(&value);
	if (SUCCEEDED(store->GetValue(FriendlyNameKey, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
		name = NarrowUtf8(std::wstring(value.pwszVal));
	}

	PropVariantClear(&value);
	return name;
}


// Whether the endpoint reports that it takes a raw stream. A property that is missing, or is not a boolean, means it does not.
bool EndpointRawSupported(IMMDevice* device) {
	ComPointer<IPropertyStore> store;
	if (FAILED(device->OpenPropertyStore(STGM_READ, store.Address()))) {
		return false;
	}

	PROPVARIANT value;
	PropVariantInit(&value);
	bool present = false;
	bool supported = false;
	if (SUCCEEDED(store->GetValue(RawProcessingSupportedKey, &value)) && value.vt == VT_BOOL) {
		present = true;
		supported = value.boolVal != VARIANT_FALSE;
	}

	PropVariantClear(&value);
	return RawModeAllowed(present, supported);
}


// Asks for a raw stream on an audio client. The request is made before the client's mix format is read and before it is initialised, which
// is where WASAPI reads it. False when the client cannot take the request or the request is refused.
bool SetRawProperties(IAudioClient* audioClient) {
	ComPointer<IAudioClient2> client2;
	if (FAILED(audioClient->QueryInterface(__uuidof(IAudioClient2), reinterpret_cast<void**>(client2.Address())))) {
		return false;
	}

	AudioClientProperties properties = {};
	properties.cbSize = sizeof(AudioClientProperties);
	properties.bIsOffload = FALSE;
	properties.eCategory = AudioCategory_Other;
	properties.Options = AUDCLNT_STREAMOPTIONS_RAW;
	return SUCCEEDED(client2->SetClientProperties(&properties));
}


// One attempt at a shared stream. The client is activated and, when wantRaw is set, asked for raw before the mix format is read, which is
// where WASAPI reads it. granted is true when the raw request was accepted. stage is the step that ended the attempt, and Ready when it
// opened. The stream is initialised but not started. The client is reset when the attempt fails.
bool InitializeShared(
	IMMDevice* device,
	bool wantRaw,
	bool& granted,
	ComPointer<IAudioClient>& client,
	PcmFormat& format,
	InitStage& stage,
	std::string& error)
{
	client.Reset();
	granted = false;
	stage = InitStage::Activate;
	HRESULT result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.Address()));
	if (FAILED(result)) {
		error = HresultText("the audio client could not be activated", result);
		client.Reset();
		return false;
	}

	if (wantRaw) {
		granted = SetRawProperties(client.Get());
	}

	stage = InitStage::MixFormat;
	WAVEFORMATEX* mix = nullptr;
	result = client->GetMixFormat(&mix);
	if (FAILED(result) || mix == nullptr) {
		error = HresultText("the mix format could not be read", result);
		client.Reset();
		return false;
	}

	stage = InitStage::Decode;
	bool ready = PcmFormatOf(mix, format, error);
	if (ready) {
		stage = InitStage::Initialize;
		result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, SharedBufferHns, 0, mix, nullptr);
		if (FAILED(result)) {
			error = HresultText("the shared-mode stream could not be initialised", result);
			ready = false;
		}
	}

	CoTaskMemFree(mix);
	if (ready) {
		stage = InitStage::Ready;
	} else {
		client.Reset();
	}

	return ready;
}


// The file name of a process's executable, or the process ID when Windows does not give the name. The limited right is enough to read
// the name, and it is granted for the processes of other users too.
std::string ProcessNameOf(DWORD processId) {
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (process == nullptr) {
		return "process " + std::to_string(processId);
	}

	std::wstring path(1024, L'\0');
	DWORD length = static_cast<DWORD>(path.size());
	bool named = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
	CloseHandle(process);
	if (!named) {
		return "process " + std::to_string(processId);
	}

	path.resize(length);
	std::size_t slash = path.find_last_of(L'\\');
	std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
	return NarrowUtf8(file);
}

}


EndpointLookup FindBoardEndpoint(const BoardInfo& board, EDataFlow flow, BoardEndpoint& endpoint, std::string& problem) {
	// The endpoints are matched to the board's USB device node by container ID, which Windows gives both alike. The name is never used.
	std::string boardInstance;
	bool nodeFound = false;
	std::string error;
	if (!FindBoardNode(board, boardInstance, nodeFound, error)) {
		problem = "the board's USB device could not be listed: " + error;
		return EndpointLookup::Failed;
	}

	if (!nodeFound) {
		problem = "the board's USB device is not attached";
		return EndpointLookup::Failed;
	}

	std::string boardContainer;
	if (!ContainerIdOfInstance(boardInstance, boardContainer, error)) {
		problem = error;
		return EndpointLookup::Failed;
	}

	if (!HasContainerId(boardContainer)) {
		problem = "the board's USB device has no container ID, so its audio endpoints cannot be told from another board's";
		return EndpointLookup::Failed;
	}

	ComPointer<IMMDeviceEnumerator> enumerator;
	HRESULT result = CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		nullptr,
		CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		reinterpret_cast<void**>(enumerator.Address()));
	if (FAILED(result)) {
		problem = HresultText("the audio device enumerator could not be created", result);
		return EndpointLookup::Failed;
	}

	ComPointer<IMMDeviceCollection> collection;
	result = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.Address());
	if (FAILED(result)) {
		problem = HresultText("the audio endpoints could not be listed", result);
		return EndpointLookup::Failed;
	}

	UINT count = 0;
	if (FAILED(collection->GetCount(&count))) {
		problem = "the number of audio endpoints could not be read";
		return EndpointLookup::Failed;
	}

	std::vector<ComPointer<IMMDevice>> devices;
	std::vector<std::string> containers;
	for (UINT index = 0; index < count; index++) {
		ComPointer<IMMDevice> device;
		if (FAILED(collection->Item(index, device.Address()))) {
			continue;
		}

		containers.push_back(ContainerIdOfEndpoint(device.Get()));
		devices.push_back(std::move(device));
	}

	std::vector<std::size_t> matches = MatchingEndpoints(boardContainer, containers);
	if (matches.empty()) {
		problem = "no active endpoint of this board: " + std::to_string(count) + " active in this direction, none has the board's container ID";
		return EndpointLookup::None;
	}

	if (matches.size() > 1) {
		problem = "several active endpoints of this board are listed, so the one to use is not known";
		return EndpointLookup::Several;
	}

	endpoint.name = FriendlyNameOf(devices[matches[0]].Get());
	endpoint.device = std::move(devices[matches[0]]);
	return EndpointLookup::Found;
}


bool OpenSharedStream(IMMDevice* device, ComPointer<IAudioClient>& client, PcmFormat& format, StreamMode& mode, std::string& error) {
	client.Reset();
	mode = StreamMode::NoRawMode;
	InitStage stage = InitStage::Activate;
	if (!EndpointRawSupported(device)) {
		bool unused = false;
		return InitializeShared(device, false, unused, client, format, stage, error);
	}

	// The endpoint takes raw streams, so the raw request is made first. A request that is refused leaves the stream on the Windows mix,
	// and the mode says the raw request did not hold.
	mode = StreamMode::Refused;
	bool granted = false;
	if (InitializeShared(device, true, granted, client, format, stage, error)) {
		if (granted) {
			mode = StreamMode::Raw;
		}

		return true;
	}

	if (!RetryWithoutRaw(granted, stage)) {
		return false;
	}

	// The raw request was granted, but the stream could not be initialised raw. It is opened once more on the Windows mix, and the mode
	// says the granted request fell back.
	std::string rawError = error;
	bool retryGranted = false;
	if (InitializeShared(device, false, retryGranted, client, format, stage, error)) {
		mode = StreamMode::FellBack;
		return true;
	}

	error = RetryFailureText(rawError, error);
	return false;
}


bool ReadEndpointVolume(IMMDevice* device, EndpointVolume& volume, std::string& error) {
	ComPointer<IAudioEndpointVolume> endpointVolume;
	HRESULT result = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(endpointVolume.Address()));
	if (FAILED(result)) {
		error = HresultText("the endpoint volume could not be opened", result);
		return false;
	}

	BOOL mute = FALSE;
	result = endpointVolume->GetMute(&mute);
	if (FAILED(result)) {
		error = HresultText("the mute state could not be read", result);
		return false;
	}

	float masterDb = 0.0f;
	volume.muted = mute != FALSE;
	volume.volumeKnown = SUCCEEDED(endpointVolume->GetMasterVolumeLevel(&masterDb));
	volume.masterDb = static_cast<double>(masterDb);
	return true;
}


bool ActiveSpeakerSessions(IMMDevice* device, std::vector<std::string>& sessions, std::string& error) {
	sessions.clear();
	ComPointer<IAudioSessionManager2> manager;
	HRESULT result = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(manager.Address()));
	if (FAILED(result)) {
		error = HresultText("the speaker's session manager could not be opened", result);
		return false;
	}

	ComPointer<IAudioSessionEnumerator> enumerator;
	result = manager->GetSessionEnumerator(enumerator.Address());
	if (FAILED(result)) {
		error = HresultText("the speaker's session list could not be read", result);
		return false;
	}

	int count = 0;
	result = enumerator->GetCount(&count);
	if (FAILED(result)) {
		error = HresultText("the number of the speaker's sessions could not be read", result);
		return false;
	}

	std::uint32_t ownProcessId = static_cast<std::uint32_t>(GetCurrentProcessId());
	for (int index = 0; index < count; index++) {
		ComPointer<IAudioSessionControl> control;
		if (FAILED(enumerator->GetSession(index, control.Address()))) {
			continue;
		}

		ComPointer<IAudioSessionControl2> control2;
		if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(control2.Address())))) {
			continue;
		}

		AudioSessionState state = AudioSessionStateInactive;
		DWORD processId = 0;
		if (FAILED(control2->GetState(&state)) || FAILED(control2->GetProcessId(&processId))) {
			continue;
		}

		std::uint32_t sessionProcessId = static_cast<std::uint32_t>(processId);
		if (!IsForeignActiveSession(state == AudioSessionStateActive, sessionProcessId, ownProcessId)) {
			continue;
		}

		// The system-sounds session carries Windows' own alert sounds. It is not an application that holds the speaker, so it is left out.
		if (control2->IsSystemSoundsSession() == S_OK) {
			continue;
		}

		sessions.push_back(ProcessNameOf(processId));
	}

	return true;
}
