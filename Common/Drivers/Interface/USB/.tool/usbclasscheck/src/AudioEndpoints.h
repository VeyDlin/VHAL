#pragma once

#include <string>
#include <vector>

#include "WinAudioCom.h"

#include <audioclient.h>
#include <mmdeviceapi.h>

#include "AudioFormat.h"
#include "AudioLogic.h"
#include "DeviceIdentity.h"

// An active audio endpoint of the board: the device, and the name the sound settings show for it.
struct BoardEndpoint {
	std::string name;
	ComPointer<IMMDevice> device;
};


// The outcome of looking for the board's endpoint of one flow. None means no active endpoint of the board is listed yet, which is the
// outcome the check waits on: Windows lists a board's endpoints some seconds after the board enumerates.
enum class EndpointLookup {
	Found,
	None,
	Several,
	Failed
};

// Finds the one active endpoint of the flow that belongs to this board: eCapture is the microphone, eRender the speaker. An endpoint
// belongs to the board when its container ID is the board's USB device node's container ID, so the name is never used. problem says
// why there is none, or that several belong to the board.
EndpointLookup FindBoardEndpoint(const BoardInfo& board, EDataFlow flow, BoardEndpoint& endpoint, std::string& problem);

// Opens a shared-mode stream on the endpoint at its mix format, which is the format the endpoint runs at. The stream is initialised
// but not started, so it makes no sound. format gets the layout, and error the reason when it fails. mode is Raw when the stream was
// asked to be raw and the endpoint granted it, so Windows' effects on the endpoint are bypassed. When the endpoint reports raw
// processing but the request is refused, the stream is opened on the Windows mix and mode is Refused. When the request is granted but
// the stream cannot be initialised raw, it is opened once more on the Windows mix and mode is FellBack. An endpoint that reports no raw
// processing is opened as usual, with mode NoRawMode.
bool OpenSharedStream(IMMDevice* device, ComPointer<IAudioClient>& client, PcmFormat& format, StreamMode& mode, std::string& error);

// The endpoint's mute and master volume in Windows. Reading them changes nothing. False with error when the mute cannot be read; a master
// volume that cannot be read only clears volume.volumeKnown.
bool ReadEndpointVolume(IMMDevice* device, EndpointVolume& volume, std::string& error);

// The names of the other applications that have an active session on the render endpoint, one per session: the process's file name. This
// process's own sessions and the system-sounds session are left out. False with error when they cannot be read.
bool ActiveSpeakerSessions(IMMDevice* device, std::vector<std::string>& sessions, std::string& error);
