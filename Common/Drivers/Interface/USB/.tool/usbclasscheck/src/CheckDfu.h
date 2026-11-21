#pragma once

#include <string>

#include "DeviceIdentity.h"
#include "Report.h"

// Downloads a random file the size of the DFU region with dfu-util, uploads the region, and compares. Prints the file's CRC-32
// for the board's console. When consolePort is not empty, the console is read during the run and its CRC line is compared.
void RunDfuCheck(const BoardInfo& board, const std::string& dfuUtilPath, const std::string& consolePort, CheckReport& report);
