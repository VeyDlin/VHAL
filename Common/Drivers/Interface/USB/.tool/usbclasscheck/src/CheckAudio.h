#pragma once

#include "DeviceIdentity.h"
#include "Report.h"

// Checks the audio demo on one board. It finds the board's microphone and speaker in Windows audio, records the microphone for a
// second and checks the board's 1 kHz tone, then plays a 440 Hz tone into the board's speaker and checks that the microphone switches
// to it (the loopback). Only the board's own endpoints are used. Nothing is changed in Windows: no default device and no volume, and
// the speaker's sessions are only read, so another application's sound is reported rather than stopped.
void RunAudioCheck(const BoardInfo& board, CheckReport& report);
