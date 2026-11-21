#pragma once

#include "ExitCodes.h"
#include "Options.h"

// Runs the command. It creates the backend, chooses the device (or every device with --all) and returns one of the Exit
// codes.
int RunCommand(const Options& options);
