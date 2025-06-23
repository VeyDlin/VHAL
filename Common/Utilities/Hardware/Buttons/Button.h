#pragma once
#include "Core.h"
#include <chrono>

namespace Buttons {

class Button {
private:
    AGPIO& gpio;
    bool lastState;
    bool currentState;
    bool rawState;
    std::chrono::milliseconds lastChangeTime;
    std::chrono::milliseconds debounceTime;
    
public:
    Button(AGPIO& gpioAdapter) 
        : gpio(gpioAdapter), lastState(false), currentState(false), 
          rawState(false), lastChangeTime(0), debounceTime(50) {}
    
    void Update(std::chrono::milliseconds currentTime) {
        lastState = currentState;
        bool newRawState = !gpio.GetState(); // Active low (button pressed = GND)
        
        // Debounce filter
        if (newRawState != rawState) {
            rawState = newRawState;
            lastChangeTime = currentTime;
        } else if ((currentTime - lastChangeTime) >= debounceTime) {
            currentState = rawState;
        }
    }
    
    void SetDebounceTime(std::chrono::milliseconds time) { debounceTime = time; }
    
    bool IsPressed() const { return currentState; }
    bool WasPressed() const { return !lastState && currentState; }  // Rising edge
    bool WasReleased() const { return lastState && !currentState; } // Falling edge
    bool HasChanged() const { return lastState != currentState; }
};

}
