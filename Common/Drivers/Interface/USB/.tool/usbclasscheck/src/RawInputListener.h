#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <windows.h>

#include "RawInputLogic.h"

// Listens for the keyboard and mouse input of one board through Raw Input. A message-only window registers the keyboard and
// the mouse with RIDEV_INPUTSINK, so the input arrives whether or not the window has focus. Events of other devices are dropped,
// so the check sees only the board's keys and moves.
class RawInputListener {
private:
	std::uint16_t productId = 0;
	std::string serial;
	HWND window = nullptr;
	bool classRegistered = false;
	bool inputRegistered = false;
	// Whether a Raw Input device handle belongs to the board. Each handle is looked up once.
	std::map<HANDLE, bool> boardDevices;
	std::vector<KeyEvent> keys;
	std::vector<MouseEvent> mouse;


public:
	RawInputListener(std::uint16_t boardProductId, const std::string& boardSerial);

	RawInputListener(const RawInputListener&) = delete;
	RawInputListener& operator=(const RawInputListener&) = delete;

	~RawInputListener();

	bool Start(std::string& error);

	// Handles the messages for about the given time, so WM_INPUT events are collected.
	void Pump(std::uint32_t milliseconds);

	void ClearEvents();

	const std::vector<KeyEvent>& Keys() const;

	const std::vector<MouseEvent>& Mouse() const;


private:
	static LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);

	void HandleInput(HRAWINPUT input);

	bool IsBoardDevice(HANDLE device);
};
