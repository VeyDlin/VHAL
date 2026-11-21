#pragma once

#include <string>
#include <vector>

#include "NetworkLogic.h"

// Every network adapter that has an IPv4 family: its name, driver description, MAC, state, link speeds and addresses. Read only;
// nothing is changed on the PC.
bool EnumerateAdapters(std::vector<AdapterInfo>& adapters, std::string& error);

// The IPv4 entries of the neighbour (ARP) table, on every adapter.
bool EnumerateArpEntries(std::vector<ArpEntry>& entries, std::string& error);
