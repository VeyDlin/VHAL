#pragma once
#include "Click.h"
#include "Action.h"
#include <initializer_list>
#include <array>
#include <functional>
#include <chrono>

namespace Buttons {

template<size_t MaxSequences = 8>
class Event {
public:
    template<size_t ClickMaxButtons = 4>
    struct ClickAction {
        Click<ClickMaxButtons>* click = nullptr;
        Action action;
        std::array<Button*, 4> holdConditions = {};
        size_t holdCount = 0;

        ClickAction() = default;
        
        ClickAction(Click<ClickMaxButtons>& clickRef, Action& actionRef) : click(&clickRef), action(actionRef) {
            holdConditions.fill(nullptr);
        }
        ClickAction(Click<ClickMaxButtons>& clickRef, const Action& actionRef) : click(&clickRef), action(actionRef) {
            holdConditions.fill(nullptr);
        }
        
        ClickAction& WhileHolding(Button& button) {
            if (holdCount < 4) {
                holdConditions[holdCount++] = &button;
            }
            return *this;
        }
        
        ClickAction& WhileHolding(std::initializer_list<std::reference_wrapper<Button>> buttons) {
            for (auto& buttonRef : buttons) {
                if (holdCount < 4) {
                    holdConditions[holdCount++] = &buttonRef.get();
                }
            }
            return *this;
        }
        
        bool AreHoldConditionsMet() const {
            for (size_t i = 0; i < holdCount; i++) {
                if (!holdConditions[i]->IsPressed()) {
                    return false;
                }
            }
            return true;
        }
    };
    
    enum class State {
        Idle,
        WaitingForSequence,
        Completed,
        Failed
    };
    
private:
    std::array<ClickAction<>, MaxSequences> clickActions;
    size_t sequenceCount = 0;
    std::function<void()> onDoneHandle = nullptr;
    std::function<void()> onFailedHandle = nullptr;
    
    State currentState = State::Idle;
    bool enabled = false;
    size_t currentSequenceIndex = 0;
    uint8 currentClickCount = 0;
    std::chrono::milliseconds timeout{5000}; // 5 second timeout for sequence
    std::chrono::milliseconds lastActivityTime{0};
    std::chrono::milliseconds shortThreshold{500};
    std::chrono::milliseconds longThreshold{2000};
    std::chrono::milliseconds multiClickGap{300};
    
    std::array<Button*, 4> globalHoldConditions;
    size_t globalHoldCount = 0;
    
public:
    Event(std::initializer_list<ClickAction<>> actionList) {
        globalHoldConditions.fill(nullptr);
        for (const auto& ca : actionList) {
            if (sequenceCount < MaxSequences) {
                clickActions[sequenceCount++] = ca;
            }
        }
    }
    
    Event(ClickAction<>& clickAction) {
        clickActions[0] = clickAction;
        sequenceCount = 1;
    }
    
    Event(const ClickAction<>& clickAction) {
        clickActions[0] = clickAction;
        sequenceCount = 1;
    }
    
    void Enable() { 
        enabled = true; 
        Reset();
    }
    
    void Disable() { 
        enabled = false; 
        currentState = State::Idle;
    }
    
    void Reset() {
        currentState = State::Idle;
        currentSequenceIndex = 0;
        currentClickCount = 0;
        lastActivityTime = std::chrono::milliseconds{0};
    }
    
    void WhileHolding(Button& button) {
        if (globalHoldCount < 4) {
            globalHoldConditions[globalHoldCount++] = &button;
        }
    }
    
    void WhileHolding(std::initializer_list<std::reference_wrapper<Button>> buttons) {
        for (auto& buttonRef : buttons) {
            if (globalHoldCount < 4) {
                globalHoldConditions[globalHoldCount++] = &buttonRef.get();
            }
        }
    }
    
    void SetTimeThresholds(std::chrono::milliseconds shortMs, std::chrono::milliseconds longMs) {
        shortThreshold = shortMs;
        longThreshold = longMs;
    }
    
    void SetMultiClickGap(std::chrono::milliseconds gap) {
        multiClickGap = gap;
    }
    
    void SetOnDoneHandle(std::function<void()> handler) { onDoneHandle = handler; }
    void SetOnFailedHandle(std::function<void()> handler) { onFailedHandle = handler; }
    void SetTimeout(std::chrono::milliseconds time) { timeout = time; }
    
    State GetState() const { return currentState; }
    bool IsEnabled() const { return enabled; }


    template<typename Func>
    void ForEachClick(Func func) {
        for (size_t index = 0; index < sequenceCount; index++) {
            if (clickActions[index].click != nullptr) {
                func(clickActions[index].click);
            }
        }
    }


    void Update(std::chrono::milliseconds currentTime, std::chrono::milliseconds deltaTime) {
        (void)deltaTime;
        if (!enabled || sequenceCount == 0) {
            return;
        }
        if (currentState == State::Completed || currentState == State::Failed) {
            Reset();
        }
        ClickAction<>& current = clickActions[currentSequenceIndex];
        if (current.click == nullptr) {
            return;
        }
        if (currentState == State::WaitingForSequence &&
            (currentTime - lastActivityTime > timeout ||
             (currentClickCount > 0 && !current.click->IsPressed() &&
              currentTime - lastActivityTime > multiClickGap))) {
            currentState = State::Failed;
            if (onFailedHandle) {
                onFailedHandle();
            }
            return;
        }
        if (!AreGlobalHoldConditionsMet() || !current.AreHoldConditionsMet()) {
            Reset();
            return;
        }
        if (current.click->WasJustPressed()) {
            currentState = State::WaitingForSequence;
            lastActivityTime = currentTime;
        }
        if (currentState != State::WaitingForSequence || !current.click->WasJustReleased()) {
            return;
        }
        if (!current.action.IsValidDuration(current.click->GetPressDuration(), shortThreshold, longThreshold)) {
            currentState = State::Failed;
            if (onFailedHandle) {
                onFailedHandle();
            }
            return;
        }
        lastActivityTime = currentTime;
        currentClickCount++;
        if (currentClickCount < current.action.GetClicksCount()) {
            return;
        }
        currentClickCount = 0;
        currentSequenceIndex++;
        if (currentSequenceIndex == sequenceCount) {
            currentState = State::Completed;
            if (onDoneHandle) {
                onDoneHandle();
            }
        }
    }
    
    // Iterate through all buttons used in this event
    template<typename Func>
    void ForEachButton(Func func) {
        // Process buttons from all click actions
        for (size_t i = 0; i < sequenceCount; i++) {
            auto& ca = clickActions[i];
            
            // Process buttons from the click
            ca.click->ForEachButton(func);
            
            // Process hold condition buttons
            for (size_t j = 0; j < ca.holdCount; j++) {
                if (ca.holdConditions[j]) {
                    func(ca.holdConditions[j]);
                }
            }
        }
        
        // Process global hold condition buttons
        for (size_t i = 0; i < globalHoldCount; i++) {
            if (globalHoldConditions[i]) {
                func(globalHoldConditions[i]);
            }
        }
    }
    
private:
    bool AreGlobalHoldConditionsMet() const {
        for (size_t i = 0; i < globalHoldCount; i++) {
            if (!globalHoldConditions[i]->IsPressed()) {
                return false;
            }
        }
        return true;
    }
    

};

using EventDefault = Event<8>;

}
