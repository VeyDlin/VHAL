#pragma once

#include "DeviceIdentity.h"
#include "Report.h"

// Checks the storage demo's disk: the volume, its two files, and a test file written, read back uncached, and deleted.
void RunStorageCheck(const BoardInfo& board, CheckReport& report);
