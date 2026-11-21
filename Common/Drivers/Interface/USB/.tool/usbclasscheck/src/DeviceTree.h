#pragma once

#include <string>
#include <vector>

#include <windows.h>

#include "DeviceIdentity.h"
#include "VolumeAccess.h"

// A GUID from its text form, such as {53B72DEF-1477-422A-B645-D5267E3E44A6}.
bool ParseGuidText(const std::string& text, GUID& guid);

// The device interface paths of one interface GUID, narrowed to ASCII.
bool EnumerateInterfacePaths(const GUID& interfaceGuid, std::vector<std::string>& paths, std::string& error);

// Every demo board on the bus: the USB device nodes of vendor 1209 with one of the demo product IDs.
bool EnumerateBoards(std::vector<BoardInfo>& boards, std::string& error);

// The serial number of the board that a device instance belongs to. The instance itself may be the board's interface, or a
// function below it, so the walk goes up the device tree until it reaches the USB device node. found is false when there is
// no board above the instance.
bool SerialOfInstance(const std::string& instance, std::string& serial, bool& found, std::string& error);

// The USB device node above a device instance: the first ancestor that is a whole USB device (not one of its interfaces), with its
// instance ID. found is false when no USB device node is above the instance, for example a disk on a SATA port.
bool UsbDeviceNodeAbove(const std::string& instance, std::string& node, bool& found, std::string& error);

// The device instance ID of the board's USB device node, found among the USB device interfaces. found is false when the board is not
// attached. The board's serial must be set, because it is what tells the board from another one of its demo.
bool FindBoardNode(const BoardInfo& board, std::string& instance, bool& found, std::string& error);

// The container ID of a device instance as GUID text (see GuidText). It is empty when Windows gives the device none, which is not an
// error. error is set only when the device cannot be read.
bool ContainerIdOfInstance(const std::string& instance, std::string& containerId, std::string& error);

// The device instance ID of the disk with this disk number, found through the disk device interfaces. found is false when no disk
// in the device tree has the number.
bool FindDiskInstance(const DiskNumber& number, std::string& instance, bool& found, std::string& error);

// The device instance ID of a Raw Input device handle.
bool InstanceOfRawDevice(HANDLE device, std::string& instance, std::string& error);

struct PortInfo {
	std::string instance;
	std::string portName;
};

// The COM ports: the devices of the Ports class that have a PortName.
bool EnumeratePorts(std::vector<PortInfo>& ports, std::string& error);
