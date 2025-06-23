#pragma once
#include <BSP.h>
#include "Action.h"
#include "Button.h"
#include "Click.h"
#include "Event.h"
#include <array>
#include <chrono>


/**
 * @brief Button handling system for embedded applications
 * 
 * This system provides sophisticated button input handling with support for:
 * - Hardware debouncing
 * - Single and multi-button combinations
 * - Click types (short, long, very long)
 * - Complex sequences with timing validation
 * - Hold conditions (global and per-action)
 * - Event-driven callbacks
 * 
 * Architecture:
 * - Button: Physical button with debounce filtering
 * - Click: Single or multiple buttons pressed together
 * - Action: Type and count of clicks (e.g., 3 short clicks)
 * - Event: Sequence of Click+Action pairs with callbacks
 * - Core: Central manager for all events and timing
 * 
 * Example usage:
 * @code
 * // Create GPIO adapters
 * AGPIO gpioVolumeUp(GPIOA, 0);
 * AGPIO gpioVolumeDown(GPIOA, 1); 
 * AGPIO gpioPower(GPIOB, 0);
 * AGPIO gpioHome(GPIOB, 1);
 * 
 * // Create buttons (using references)
 * Buttons::Button volumeUp(gpioVolumeUp);
 * Buttons::Button volumeDown(gpioVolumeDown);
 * Buttons::Button power(gpioPower);
 * Buttons::Button home(gpioHome);
 * 
 * // Create click combinations
 * Buttons::ClickDefault clickVolumeUp(volumeUp);
 * Buttons::ClickDefault clickCombo({volumeUp, volumeDown}); // Both pressed together
 * 
 * // Event 1: Factory reset - hold power and press volume up 5 times
 * Buttons::EventDefault factoryReset({
 *     Buttons::MakeClickAction(clickVolumeUp,
 *         Buttons::Action(Buttons::Action::ClickType::Short, 5))
 * });
 * factoryReset.WhileHolding(power); // Must hold power during entire sequence
 * factoryReset.SetOnDoneHandle([]() {
 *     System::FactoryReset();
 * });
 * 
 * // Event 2: Secret menu - complex sequence with hold conditions
 * Buttons::EventDefault secretMenu({
 *     // Long press home while holding volume down
 *     Buttons::MakeClickActionWithHold(Buttons::ClickDefault(home),
 *         Buttons::Action(Buttons::Action::ClickType::Long, 1), {volumeDown}),
 *     // Then 3 short presses of volume up+down combo
 *     Buttons::MakeClickAction(clickCombo,
 *         Buttons::Action(Buttons::Action::ClickType::Short, 3))
 * });
 * secretMenu.SetOnDoneHandle([]() {
 *     UI::ShowSecretMenu();
 * });
 * 
 * // Create button handler
 * Buttons::Core<10> buttonCore; // Max 10 events
 * 
 * // Configure timing (using std::chrono literals)
 * using namespace std::chrono_literals;
 * buttonCore.debounceTime = 20ms;           // Debounce filter
 * buttonCore.shortClickThreshold = 400ms;   // Short < 400ms
 * buttonCore.longClickThreshold = 1000ms;   // Long >= 1000ms  
 * buttonCore.sequenceTimeout = 5000ms;      // Max time between actions
 * buttonCore.multiClickGap = 300ms;         // Max gap for multi-clicks
 * buttonCore.UpdateTimeSettings();          // Apply to all buttons/events
 * 
 * // Register events
 * buttonCore.AddEvent(factoryReset);
 * buttonCore.AddEvent(secretMenu);
 * 
 * // In main loop or RTOS task
 * while (true) {
 *     buttonCore.ProcessEvents(10ms); // Call with time delta
 *     osDelay(10);
 * }
 * @endcode
 */

namespace Buttons {

template<size_t MaxEvents = 16>
class Core {
public:
    // Public timing configuration fields
    std::chrono::milliseconds debounceTime{50};          // Contact bounce filter duration
    std::chrono::milliseconds shortClickThreshold{500};  // Click < 500ms = short
    std::chrono::milliseconds longClickThreshold{2000};  // Click >= 2000ms = very long
    std::chrono::milliseconds sequenceTimeout{5000};     // Max time between sequence actions
    std::chrono::milliseconds multiClickGap{300};        // Max gap between multi-clicks
    
private:
    std::array<EventDefault*, MaxEvents> events;
    std::array<Button*, 32> buttons;  // All unique buttons for update
    size_t eventCount = 0;
    size_t buttonCount = 0;
    std::chrono::milliseconds lastUpdateTime{0};
    
public:
    Core() {
        events.fill(nullptr);
        buttons.fill(nullptr);
    }
    
    bool AddEvent(EventDefault& event) {
        if (eventCount >= MaxEvents) return false;
        
        events[eventCount++] = &event;
        event.SetTimeout(sequenceTimeout);
        event.SetTimeThresholds(shortClickThreshold, longClickThreshold);
        event.SetMultiClickGap(multiClickGap);
        
        // Collect all unique buttons from event
        CollectButtons(event);
        
        return true;
    }
    
    bool RemoveEvent(EventDefault& event) {
        for (size_t i = 0; i < eventCount; i++) {
            if (events[i] == &event) {
                // Shift remaining events
                for (size_t j = i; j < eventCount - 1; j++) {
                    events[j] = events[j + 1];
                }
                events[--eventCount] = nullptr;
                return true;
            }
        }
        return false;
    }
    
    void EnableAll() {
        for (size_t i = 0; i < eventCount; i++) {
            if (events[i]) events[i]->Enable();
        }
    }
    
    void DisableAll() {
        for (size_t i = 0; i < eventCount; i++) {
            if (events[i]) events[i]->Disable();
        }
    }
    
    void ProcessEvents(std::chrono::milliseconds deltaTime) {
        auto currentTime = lastUpdateTime + deltaTime;
        
        // Update all buttons with debounce filtering
        for (size_t i = 0; i < buttonCount; i++) {
            if (buttons[i]) {
                buttons[i]->Update(currentTime);
            }
        }
        
        // Process all events
        for (size_t i = 0; i < eventCount; i++) {
            if (events[i] && events[i]->IsEnabled()) {
                events[i]->Update(currentTime, deltaTime);
            }
        }
        
        lastUpdateTime = currentTime;
    }
    
    size_t GetEventCount() const { return eventCount; }
    size_t GetMaxEvents() const { return MaxEvents; }
    
    EventDefault* GetEvent(size_t index) {
        return (index < eventCount) ? events[index] : nullptr;
    }
    
    // Update time settings in all buttons and events
    void UpdateTimeSettings() {
        UpdateButtonDebounce();
        UpdateEventTimeSettings();
    }
    
private:
    void CollectButtons(EventDefault& event) {
        // Collect all unique buttons from the event
        event.ForEachButton([this](Button* button) {
            AddButton(button);
        });
    }
    
    void UpdateButtonDebounce() {
        for (size_t i = 0; i < buttonCount; i++) {
            if (buttons[i]) {
                buttons[i]->SetDebounceTime(debounceTime);
            }
        }
    }
    
    void UpdateEventTimeSettings() {
        for (size_t i = 0; i < eventCount; i++) {
            if (events[i]) {
                events[i]->SetTimeout(sequenceTimeout);
                events[i]->SetTimeThresholds(shortClickThreshold, longClickThreshold);
                events[i]->SetMultiClickGap(multiClickGap);
            }
        }
    }
    
    bool AddButton(Button* button) {
        // Check if button already registered
        for (size_t i = 0; i < buttonCount; i++) {
            if (buttons[i] == button) return true;
        }
        
        if (buttonCount < 32) {
            buttons[buttonCount++] = button;
            button->SetDebounceTime(debounceTime);
            return true;
        }
        return false;
    }
    
public:
    // Manual button registration for update loop
    bool RegisterButton(Button& button) {
        return AddButton(&button);
    }
};

// Helper functions for creating ClickActions with hold conditions
template<size_t ClickMaxButtons = 4>
static auto MakeClickAction(ClickDefault& click, const Action& action) {
    return EventDefault::ClickAction<>(click, action);
}

template<size_t ClickMaxButtons = 4>
static auto MakeClickActionWithHold(ClickDefault& click, const Action& action, std::initializer_list<std::reference_wrapper<Button>> holdButtons) {
    auto ca = EventDefault::ClickAction<>(click, action);
    ca.WhileHolding(holdButtons);
    return ca;
}

}
