#pragma once

#include "DeviceIdentity.h"
#include "Report.h"

// Checks the network demo on one board: the adapter that has the board's host MAC, the lease of the DHCP server, echoes through the
// leased address, and the board's ARP entry. Nothing is configured on the PC: no adapter setting and no route is changed.
void RunNetworkCheck(const BoardInfo& board, CheckReport& report);
