#pragma once
#include "Core.h"
#include <chrono>

namespace Buttons {

class Action {
public:
    enum class ClickType {
        Short,      
        Long,       
        VeryLong,   
        Infinitely, // Hold until released
        Other
    };
    
private:
    ClickType clickType = ClickType::Short;
    uint8 clicksCount = 1;
    
public:
    Action() = default;
    Action(ClickType type, uint8 count) 
        : clickType(type), clicksCount(count) {}
    
    Action& SetClickType(ClickType type) { clickType = type; return *this; }
    Action& SetClicksCount(uint8 count) { clicksCount = count; return *this; }
    Action& Get() { return *this; }
    
    ClickType GetClickType() const { return clickType; }
    uint8 GetClicksCount() const { return clicksCount; }
    
    bool IsValidDuration(std::chrono::milliseconds duration, 
                         std::chrono::milliseconds shortThreshold, 
                         std::chrono::milliseconds longThreshold) const {
        switch(clickType) {
            case ClickType::Short: return duration < shortThreshold;
            case ClickType::Long: return duration >= shortThreshold && duration < longThreshold;
            case ClickType::VeryLong: return duration >= longThreshold;
            case ClickType::Infinitely: return true;
            default: return true;
        }
    }
};

}
