#include "DeviceTree.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <cfgmgr32.h>
#include <devguid.h>
#include <devpropdef.h>
#include <objbase.h>
#include <setupapi.h>
#include <ntddstor.h>
#include <usbiodef.h>
#include <winioctl.h>

#include "DemoProducts.h"
#include "WinText.h"

namespace {

// How far up the device tree the serial search goes. A board's interface is at most two levels below the USB device node
// (the HID child of an interface), so a few levels are enough.
constexpr int MaxParentDepth = 6;


// Owns a SetupAPI device information set.
class ScopedDeviceInfo {
private:
	HDEVINFO handle = INVALID_HANDLE_VALUE;


public:
	explicit ScopedDeviceInfo(HDEVINFO value) {
		handle = value;
	}


	ScopedDeviceInfo(const ScopedDeviceInfo&) = delete;
	ScopedDeviceInfo& operator=(const ScopedDeviceInfo&) = delete;


	~ScopedDeviceInfo() {
		if (handle != INVALID_HANDLE_VALUE) {
			SetupDiDestroyDeviceInfoList(handle);
		}
	}


	HDEVINFO Get() const {
		return handle;
	}


	bool IsValid() const {
		return handle != INVALID_HANDLE_VALUE;
	}
};


// The path of one interface, as SetupAPI gives it (wide). deviceData, when not null, gets the device the interface belongs to.
bool ReadInterfacePath(
	HDEVINFO deviceInfo,
	SP_DEVICE_INTERFACE_DATA& interfaceData,
	std::wstring& path,
	SP_DEVINFO_DATA* deviceData,
	std::string& error)
{
	DWORD requiredSize = 0;
	SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
	if (requiredSize == 0) {
		error = "SetupDiGetDeviceInterfaceDetail failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	std::vector<std::uint8_t> storage(requiredSize);
	SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
	detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
	if (!SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, detail, requiredSize, nullptr, deviceData)) {
		error = "SetupDiGetDeviceInterfaceDetail failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	path = detail->DevicePath;
	return true;
}


// The container ID that Windows keeps for a device group (DEVPKEY_Device_ContainerId, devpropdef.h). The endpoints' property store
// has the same key, so the audio check reads both the same way.
const DEVPROPKEY ContainerIdKey = {
	{ 0x8C7ED206, 0x3F8A, 0x4827, { 0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C } },
	2
};


// The configuration manager's handle of a device instance. The instance must be in the device tree.
bool LocateNode(const std::string& instance, DEVINST& node, std::string& error) {
	std::wstring wide = WidenAscii(instance);
	std::vector<wchar_t> idBuffer(wide.begin(), wide.end());
	idBuffer.push_back(L'\0');

	CONFIGRET result = CM_Locate_DevNodeW(&node, idBuffer.data(), CM_LOCATE_DEVNODE_NORMAL);
	if (result != CR_SUCCESS) {
		error = "the device " + instance + " is not in the device tree (configuration manager code " + std::to_string(result) + ")";
		return false;
	}

	return true;
}


// The parent of a device instance. parent is left empty when the device is the root of the tree.
bool ParentOf(const std::string& instance, std::string& parent, std::string& error) {
	DEVINST node = 0;
	if (!LocateNode(instance, node, error)) {
		return false;
	}

	DEVINST parentNode = 0;
	CONFIGRET result = CM_Get_Parent(&parentNode, node, 0);
	if (result != CR_SUCCESS) {
		parent.clear();
		return true;
	}

	wchar_t parentId[MAX_DEVICE_ID_LEN] {};
	result = CM_Get_Device_IDW(parentNode, parentId, MAX_DEVICE_ID_LEN, 0);
	if (result != CR_SUCCESS) {
		error = "CM_Get_Device_ID failed for the parent of " + instance;
		return false;
	}

	parent = NarrowAscii(parentId);
	return true;
}

}


bool ParseGuidText(const std::string& text, GUID& guid) {
	std::wstring wide = WidenAscii(text);
	return CLSIDFromString(wide.c_str(), &guid) == S_OK;
}


bool EnumerateInterfacePaths(const GUID& interfaceGuid, std::vector<std::string>& paths, std::string& error) {
	ScopedDeviceInfo deviceInfo(SetupDiGetClassDevsW(&interfaceGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
	if (!deviceInfo.IsValid()) {
		error = "SetupDiGetClassDevs failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	paths.clear();
	for (DWORD index = 0;; index++) {
		SP_DEVICE_INTERFACE_DATA interfaceData {};
		interfaceData.cbSize = sizeof(interfaceData);
		if (!SetupDiEnumDeviceInterfaces(deviceInfo.Get(), nullptr, &interfaceGuid, index, &interfaceData)) {
			if (GetLastError() == ERROR_NO_MORE_ITEMS) {
				break;
			}

			error = "SetupDiEnumDeviceInterfaces failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		std::wstring path;
		if (!ReadInterfacePath(deviceInfo.Get(), interfaceData, path, nullptr, error)) {
			return false;
		}

		paths.push_back(NarrowAscii(path));
	}

	return true;
}


bool EnumerateBoards(std::vector<BoardInfo>& boards, std::string& error) {
	std::vector<std::string> paths;
	if (!EnumerateInterfacePaths(GUID_DEVINTERFACE_USB_DEVICE, paths, error)) {
		return false;
	}

	boards.clear();
	for (const std::string& path : paths) {
		InstanceParts parts;
		if (!ParseInstance(InstanceFromInterfacePath(path), parts) || !IsBoardNode(parts)) {
			continue;
		}

		if (parts.vendorId != DemoProducts::VendorId || !IsDemoProduct(parts.productId)) {
			continue;
		}

		BoardInfo board;
		board.productId = parts.productId;
		board.serial = SerialOfBoardNode(parts);
		boards.push_back(board);
	}

	return true;
}


bool UsbDeviceNodeAbove(const std::string& instance, std::string& node, bool& found, std::string& error) {
	node.clear();
	found = false;
	std::string current = instance;
	for (int depth = 0; depth < MaxParentDepth; depth++) {
		InstanceParts parts;
		if (ParseInstance(current, parts) && IsBoardNode(parts)) {
			node = current;
			found = true;
			return true;
		}

		std::string parent;
		if (!ParentOf(current, parent, error)) {
			return false;
		}

		if (parent.empty()) {
			return true;
		}

		current = parent;
	}

	return true;
}


bool SerialOfInstance(const std::string& instance, std::string& serial, bool& found, std::string& error) {
	serial.clear();
	std::string node;
	if (!UsbDeviceNodeAbove(instance, node, found, error)) {
		return false;
	}

	if (!found) {
		return true;
	}

	// The node was parsed when it was found, so it parses again.
	InstanceParts parts;
	if (!ParseInstance(node, parts)) {
		error = "the device node " + node + " has no parts";
		return false;
	}

	serial = SerialOfBoardNode(parts);
	return true;
}


bool FindBoardNode(const BoardInfo& board, std::string& instance, bool& found, std::string& error) {
	instance.clear();
	found = false;
	std::vector<std::string> paths;
	if (!EnumerateInterfacePaths(GUID_DEVINTERFACE_USB_DEVICE, paths, error)) {
		return false;
	}

	for (const std::string& path : paths) {
		std::string candidate = InstanceFromInterfacePath(path);
		std::string reason;
		if (IsNodeOfBoard(candidate, board, reason)) {
			instance = candidate;
			found = true;
			return true;
		}
	}

	return true;
}


bool ContainerIdOfInstance(const std::string& instance, std::string& containerId, std::string& error) {
	containerId.clear();
	DEVINST node = 0;
	if (!LocateNode(instance, node, error)) {
		return false;
	}

	DEVPROPTYPE type = 0;
	GUID guid {};
	ULONG size = sizeof(guid);
	CONFIGRET result = CM_Get_DevNode_PropertyW(
		node,
		&ContainerIdKey,
		&type,
		reinterpret_cast<PBYTE>(&guid),
		&size,
		0);
	if (result == CR_NO_SUCH_VALUE) {
		return true;
	}

	if (result != CR_SUCCESS || type != DEVPROP_TYPE_GUID || size != sizeof(guid)) {
		error = "the container ID of " + instance + " could not be read (configuration manager code " + std::to_string(result) + ")";
		return false;
	}

	containerId = GuidText(guid);
	return true;
}


bool FindDiskInstance(const DiskNumber& number, std::string& instance, bool& found, std::string& error) {
	ScopedDeviceInfo deviceInfo(SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
	if (!deviceInfo.IsValid()) {
		error = "SetupDiGetClassDevs (disks) failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	instance.clear();
	found = false;
	for (DWORD index = 0;; index++) {
		SP_DEVICE_INTERFACE_DATA interfaceData {};
		interfaceData.cbSize = sizeof(interfaceData);
		if (!SetupDiEnumDeviceInterfaces(deviceInfo.Get(), nullptr, &GUID_DEVINTERFACE_DISK, index, &interfaceData)) {
			if (GetLastError() == ERROR_NO_MORE_ITEMS) {
				break;
			}

			error = "SetupDiEnumDeviceInterfaces (disks) failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		std::wstring path;
		SP_DEVINFO_DATA deviceData {};
		deviceData.cbSize = sizeof(deviceData);
		if (!ReadInterfacePath(deviceInfo.Get(), interfaceData, path, &deviceData, error)) {
			return false;
		}

		// A disk that cannot be queried is not the one looked for. The search goes on, and a disk that stays unknown is never written to.
		DiskNumber candidate;
		std::string queryError;
		if (!DiskNumberOfDevice(NarrowAscii(path), candidate, queryError)) {
			continue;
		}

		if (candidate.deviceType != FILE_DEVICE_DISK || candidate.deviceNumber != number.deviceNumber) {
			continue;
		}

		DWORD requiredSize = 0;
		SetupDiGetDeviceInstanceIdW(deviceInfo.Get(), &deviceData, nullptr, 0, &requiredSize);
		std::vector<wchar_t> instanceId(requiredSize + 1, L'\0');
		if (!SetupDiGetDeviceInstanceIdW(deviceInfo.Get(), &deviceData, instanceId.data(), requiredSize, nullptr)) {
			error = "SetupDiGetDeviceInstanceId failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		instance = NarrowAscii(instanceId.data());
		found = true;
		return true;
	}

	return true;
}


bool InstanceOfRawDevice(HANDLE device, std::string& instance, std::string& error) {
	UINT length = 0;
	if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &length) == static_cast<UINT>(-1)) {
		error = "GetRawInputDeviceInfo failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	std::vector<wchar_t> name(length + 1, L'\0');
	UINT copied = GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &length);
	if (copied == static_cast<UINT>(-1)) {
		error = "GetRawInputDeviceInfo failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	instance = InstanceFromInterfacePath(NarrowAscii(name.data()));
	if (instance.empty()) {
		error = "the Raw Input device name has no device instance";
		return false;
	}

	return true;
}


bool EnumeratePorts(std::vector<PortInfo>& ports, std::string& error) {
	ScopedDeviceInfo deviceInfo(SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT));
	if (!deviceInfo.IsValid()) {
		error = "SetupDiGetClassDevs (Ports) failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	ports.clear();
	for (DWORD index = 0;; index++) {
		SP_DEVINFO_DATA data {};
		data.cbSize = sizeof(data);
		if (!SetupDiEnumDeviceInfo(deviceInfo.Get(), index, &data)) {
			if (GetLastError() == ERROR_NO_MORE_ITEMS) {
				break;
			}

			error = "SetupDiEnumDeviceInfo failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		DWORD requiredSize = 0;
		SetupDiGetDeviceInstanceIdW(deviceInfo.Get(), &data, nullptr, 0, &requiredSize);
		std::vector<wchar_t> instanceId(requiredSize + 1, L'\0');
		if (!SetupDiGetDeviceInstanceIdW(deviceInfo.Get(), &data, instanceId.data(), requiredSize, nullptr)) {
			error = "SetupDiGetDeviceInstanceId failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		HKEY key = SetupDiOpenDevRegKey(deviceInfo.Get(), &data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
		if (key == INVALID_HANDLE_VALUE) {
			continue;
		}

		wchar_t portName[64] {};
		DWORD valueBytes = sizeof(portName);
		DWORD valueType = 0;
		LONG status = RegQueryValueExW(key, L"PortName", nullptr, &valueType, reinterpret_cast<LPBYTE>(portName), &valueBytes);
		RegCloseKey(key);
		if (status != ERROR_SUCCESS || valueType != REG_SZ) {
			continue;
		}

		PortInfo port;
		port.instance = NarrowAscii(instanceId.data());
		port.portName = NarrowAscii(portName);
		ports.push_back(port);
	}

	return true;
}
