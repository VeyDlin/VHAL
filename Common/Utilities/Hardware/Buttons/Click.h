#pragma once
#include "Core.h"
#include "Button.h"
#include <initializer_list>
#include <array>
#include <functional>
#include <chrono>

namespace Buttons {

template<size_t MaxButtons = 4>
class Click {
private:
    std::array<Button*, MaxButtons> buttons;
    size_t buttonCount = 0;
    std::function<void()> onPushHandle = nullptr;
    std::function<void()> onPullHandle = nullptr;
    
    std::chrono::milliseconds pressStartTime{0};
    std::chrono::milliseconds pressDuration{0};
    bool isPressed = false;
    bool wasPressed = false;
    
public:
    Click(std::initializer_list<std::reference_wrapper<Button>> buttonList) {
        buttons.fill(nullptr);
        for (auto& buttonRef : buttonList) {
            if (buttonCount < MaxButtons) {
                buttons[buttonCount++] = &buttonRef.get();
            }
        }
    }
    
    Click(Button& button) {
        buttons.fill(nullptr);
        buttons[0] = &button;
        buttonCount = 1;
    }
    
    void SetOnPushHandle(std::function<void()> handler) { onPushHandle = handler; }
    void SetOnPullHandle(std::function<void()> handler) { onPullHandle = handler; }
    
    void Update(std::chrono::milliseconds currentTime) {
        // Check if all buttons in combination are pressed
        bool allPressed = true;
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
            if (onPushHandle) onPushHandle();
        }
        // Falling edge - combination just released
        else if (!allPressed && isPressed) {
            isPressed = false;
            pressDuration = currentTime - pressStartTime;
            if (onPullHandle) onPullHandle();
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
