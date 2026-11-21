#include "RawInputListener.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "DemoProducts.h"
#include "DeviceIdentity.h"
#include "DeviceTree.h"
#include "WinText.h"

namespace {

// The window class name. It is registered by Start and removed by the destructor.
const wchar_t* ClassName = L"VhalUsbClassCheckRawInput";

// The HID usage page and the usages of the generic desktop controls: keyboard (6) and mouse (2).
constexpr USHORT HidPageGenericDesktop = 0x01;
constexpr USHORT HidUsageMouse = 0x02;
constexpr USHORT HidUsageKeyboard = 0x06;

}


RawInputListener::RawInputListener(std::uint16_t boardProductId, const std::string& boardSerial) {
	productId = boardProductId;
	serial = boardSerial;
}


RawInputListener::~RawInputListener() {
	if (inputRegistered) {
		RAWINPUTDEVICE remove[2] {};
		remove[0].usUsagePage = HidPageGenericDesktop;
		remove[0].usUsage = HidUsageKeyboard;
		remove[0].dwFlags = RIDEV_REMOVE;
		remove[0].hwndTarget = nullptr;
		remove[1].usUsagePage = HidPageGenericDesktop;
		remove[1].usUsage = HidUsageMouse;
		remove[1].dwFlags = RIDEV_REMOVE;
		remove[1].hwndTarget = nullptr;
		RegisterRawInputDevices(remove, 2, sizeof(RAWINPUTDEVICE));
	}

	if (window != nullptr) {
		DestroyWindow(window);
	}

	if (classRegistered) {
		UnregisterClassW(ClassName, GetModuleHandleW(nullptr));
	}
}


bool RawInputListener::Start(std::string& error) {
	HINSTANCE instance = GetModuleHandleW(nullptr);
	WNDCLASSEXW windowClass {};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.lpfnWndProc = &RawInputListener::WindowProc;
	windowClass.hInstance = instance;
	windowClass.lpszClassName = ClassName;
	if (RegisterClassExW(&windowClass) == 0) {
		error = "RegisterClassEx failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	classRegistered = true;
	// The window's create parameter is this object, so WindowProc can reach the events.
	window = CreateWindowExW(0, ClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
	if (window == nullptr) {
		error = "CreateWindowEx failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	RAWINPUTDEVICE devices[2] {};
	devices[0].usUsagePage = HidPageGenericDesktop;
	devices[0].usUsage = HidUsageKeyboard;
	devices[0].dwFlags = RIDEV_INPUTSINK;
	devices[0].hwndTarget = window;
	devices[1].usUsagePage = HidPageGenericDesktop;
	devices[1].usUsage = HidUsageMouse;
	devices[1].dwFlags = RIDEV_INPUTSINK;
	devices[1].hwndTarget = window;
	if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) {
		error = "RegisterRawInputDevices failed: " + FormatWindowsError(GetLastError());
		return false;
	}

	inputRegistered = true;
	return true;
}


void RawInputListener::Pump(std::uint32_t milliseconds) {
	ULONGLONG deadline = GetTickCount64() + milliseconds;
	while (true) {
		MSG message {};
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		ULONGLONG now = GetTickCount64();
		if (now >= deadline) {
			break;
		}

		MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(deadline - now), QS_ALLINPUT);
	}
}


void RawInputListener::ClearEvents() {
	keys.clear();
	mouse.clear();
}


const std::vector<KeyEvent>& RawInputListener::Keys() const {
	return keys;
}


const std::vector<MouseEvent>& RawInputListener::Mouse() const {
	return mouse;
}


LRESULT CALLBACK RawInputListener::WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_NCCREATE) {
		CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
	}

	RawInputListener* self = reinterpret_cast<RawInputListener*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
	if (self != nullptr && message == WM_INPUT) {
		self->HandleInput(reinterpret_cast<HRAWINPUT>(lParam));
	}

	return DefWindowProcW(windowHandle, message, wParam, lParam);
}


void RawInputListener::HandleInput(HRAWINPUT input) {
	UINT size = 0;
	if (GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
		return;
	}

	// Raw Input structures need pointer alignment, so the buffer is made of 64-bit words.
	std::vector<std::uint64_t> buffer((size + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
	if (GetRawInputData(input, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size) {
		return;
	}

	RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
	if (!IsBoardDevice(raw->header.hDevice)) {
		return;
	}

	if (raw->header.dwType == RIM_TYPEKEYBOARD) {
		KeyEvent event;
		event.virtualKey = raw->data.keyboard.VKey;
		event.breakCode = (raw->data.keyboard.Flags & RI_KEY_BREAK) != 0;
		keys.push_back(event);
		return;
	}

	if (raw->header.dwType == RIM_TYPEMOUSE) {
		// A pointer that reports absolute positions (a touch screen) is not the board's relative mouse.
		if ((raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
			return;
		}

		MouseEvent event;
		event.dx = raw->data.mouse.lLastX;
		event.dy = raw->data.mouse.lLastY;
		if ((raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL) != 0) {
			event.wheel = static_cast<SHORT>(raw->data.mouse.usButtonData);
		}

		if (event.dx != 0 || event.dy != 0 || event.wheel != 0) {
			mouse.push_back(event);
		}
	}
}


bool RawInputListener::IsBoardDevice(HANDLE device) {
	std::map<HANDLE, bool>::const_iterator known = boardDevices.find(device);
	if (known != boardDevices.end()) {
		return known->second;
	}

	// A Raw Input device of the board is an HID child below the USB device node, so the serial is found by walking up.
	bool match = false;
	std::string instance;
	std::string error;
	if (InstanceOfRawDevice(device, instance, error)) {
		InstanceParts parts;
		if (ParseInstance(instance, parts) && parts.vendorId == DemoProducts::VendorId && parts.productId == productId) {
			std::string boardSerial;
			bool found = false;
			if (SerialOfInstance(instance, boardSerial, found, error) && found) {
				match = SerialMatches(serial, boardSerial);
			}
		}
	}

	boardDevices[device] = match;
	return match;
}
