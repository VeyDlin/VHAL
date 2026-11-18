#pragma once
#include "Button.h"
#include <array>
#include <chrono>
#include <functional>
#include <initializer_list>

namespace Buttons {


template<size_t MaxButtons = 4>
class Click {
private:
	std::array<Button*, MaxButtons> buttons;
	size_t buttonCount = 0;
	bool allButtonsAccepted = true;
	std::function<void()> onPushHandle = nullptr;
	std::function<void()> onPullHandle = nullptr;
	std::chrono::milliseconds pressStartTime{0};
	std::chrono::milliseconds pressDuration{0};
	bool isPressed = false;
	bool wasPressed = false;

public:
	Click(std::initializer_list<std::reference_wrapper<Button>> buttonList) {
		buttons.fill(nullptr);
		for (const std::reference_wrapper<Button>& buttonRef : buttonList) {
			if (!AddButton(buttonRef.get())) {
				allButtonsAccepted = false;
			}
		}
	}

	Click(Button& button) {
		buttons.fill(nullptr);
		AddButton(button);
	}

	// Returns false and leaves the click unchanged when all MaxButtons slots are used.
	bool AddButton(Button& button) {
		if (buttonCount >= MaxButtons) {
			return false;
		}
		buttons[buttonCount++] = &button;
		return true;
	}

	// False when a constructor was given more buttons than MaxButtons; the extra ones were not added.
	bool IsComplete() const { return allButtonsAccepted; }

	void SetOnPushHandle(std::function<void()> handler) { onPushHandle = handler; }

	void SetOnPullHandle(std::function<void()> handler) { onPullHandle = handler; }

	void Update(std::chrono::milliseconds currentTime) {
		// An empty combination has no buttons to hold, so it must never count as pressed.
		bool allPressed = buttonCount > 0;
		for (size_t i = 0; i < buttonCount; i++) {
			if (!buttons[i]->IsPressed()) {
				allPressed = false;
				break;
			}
		}

		wasPressed = isPressed;

		// Rising edge - combination just pressed
		if (allPressed && !isPressed) {
			isPressed = true;
			pressStartTime = currentTime;
			if (onPushHandle) {
				onPushHandle();
			}
		} else if (!allPressed && isPressed) {
			// Falling edge - combination just released
			isPressed = false;
			pressDuration = currentTime - pressStartTime;
			if (onPullHandle) {
				onPullHandle();
			}
		}

		// Update current press duration
		if (isPressed) {
			pressDuration = currentTime - pressStartTime;
		}
	}

	bool IsPressed() const { return isPressed; }

	bool WasJustPressed() const { return isPressed && !wasPressed; }

	bool WasJustReleased() const { return !isPressed && wasPressed; }

	std::chrono::milliseconds GetPressDuration() const { return pressDuration; }

	std::chrono::milliseconds GetPressStartTime() const { return pressStartTime; }

	// Iterate through all buttons in this click
	template<typename Func>
	void ForEachButton(Func func) {
		for (size_t i = 0; i < buttonCount; i++) {
			if (buttons[i]) {
				func(buttons[i]);
			}
		}
	}
};

using ClickDefault = Click<4>;

}
