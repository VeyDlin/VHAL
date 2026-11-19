#pragma once

#include "Options.h"
#include "UsbBackend.h"

inline constexpr int ExitOk = 0;
inline constexpr int ExitDevice = 1;
inline constexpr int ExitUsage = 2;
inline constexpr int ExitData = 3;

// Runs the command on a backend that is not open yet. Returns one of the Exit codes.
int RunCommand(IUsbBackend& backend, const Options& options);
