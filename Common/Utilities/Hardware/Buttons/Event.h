#pragma once
#include "Action.h"
#include "Click.h"
#include <array>
#include <chrono>
#include <functional>
#include <initializer_list>

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
		// False when MakeClickActionWithHold was given more hold buttons than fit.
		bool holdsAccepted = true;

		ClickAction() = default;

		ClickAction(Click<ClickMaxButtons>& clickRef, Action& actionRef)
			: click(&clickRef), action(actionRef) { }

		ClickAction(Click<ClickMaxButtons>& clickRef, const Action& actionRef)
			: click(&clickRef), action(actionRef) { }

		// Returns false and leaves the action unchanged when the 4 hold slots are used.
		bool WhileHolding(Button& button) {
			if (holdCount >= 4) {
				return false;
			}
			holdConditions[holdCount++] = &button;
			return true;
		}

		// All or nothing: returns false and adds none of the buttons when they do not all fit.
		bool WhileHolding(std::initializer_list<std::reference_wrapper<Button>> buttons) {
			if (holdCount + buttons.size() > 4) {
				return false;
			}
			for (const std::reference_wrapper<Button>& buttonRef : buttons) {
				holdConditions[holdCount++] = &buttonRef.get();
			}
			return true;
		}

		// A step is complete when its action is valid, its click (if any) is complete and all its holds were accepted.
		bool IsComplete() const {
			bool clickComplete = (click == nullptr) || click->IsComplete();
			return holdsAccepted && action.IsValid() && clickComplete;
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
	bool allStepsAccepted = true;
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

	std::array<Button*, 4> globalHoldConditions = {};
	size_t globalHoldCount = 0;

public:
	Event(std::initializer_list<ClickAction<>> actionList) {
		for (const ClickAction<>& ca : actionList) {
			if (!AddStep(ca)) {
				allStepsAccepted = false;
			}
		}
	}

	Event(ClickAction<>& clickAction) {
		if (!AddStep(clickAction)) {
			allStepsAccepted = false;
		}
	}

	Event(const ClickAction<>& clickAction) {
		if (!AddStep(clickAction)) {
			allStepsAccepted = false;
		}
	}

	// Returns false and leaves the event unchanged when all MaxSequences steps are used or the step is not complete.
	bool AddStep(const ClickAction<>& step) {
		if (sequenceCount >= MaxSequences || !step.IsComplete()) {
			return false;
		}
		clickActions[sequenceCount++] = step;
		return true;
	}

	// False when a constructor was given more steps than MaxSequences, or a step that is not complete.
	bool IsComplete() const { return allStepsAccepted; }

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

	// Returns false and leaves the event unchanged when all 4 global hold slots are used.
	bool WhileHolding(Button& button) {
		if (globalHoldCount >= 4) {
			return false;
		}
		globalHoldConditions[globalHoldCount++] = &button;
		return true;
	}

	// All or nothing: returns false and adds none of the buttons when they do not all fit.
	bool WhileHolding(std::initializer_list<std::reference_wrapper<Button>> buttons) {
		if (globalHoldCount + buttons.size() > 4) {
			return false;
		}
		for (const std::reference_wrapper<Button>& buttonRef : buttons) {
			globalHoldConditions[globalHoldCount++] = &buttonRef.get();
		}
		return true;
	}

	void SetTimeThresholds(std::chrono::milliseconds shortMs, std::chrono::milliseconds longMs) {
		shortThreshold = shortMs;
		longThreshold = longMs;
	}

	void SetMultiClickGap(std::chrono::milliseconds gap) { multiClickGap = gap; }

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

		// The timeouts limit the idle time between actions. Holding the click is activity,
		// so the idle clock keeps restarting while it is held; otherwise a long hold would
		// time out on the very tick it ends.
		if (current.click->IsPressed()) {
			lastActivityTime = currentTime;
		}
		bool idle = !current.click->IsPressed();
		bool sequenceTimedOut = currentTime - lastActivityTime > timeout;
		bool multiClickExpired = currentClickCount > 0 && currentTime - lastActivityTime > multiClickGap;
		if (currentState == State::WaitingForSequence && idle && (sequenceTimedOut || multiClickExpired)) {
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
		for (size_t i = 0; i < sequenceCount; i++) {
			ClickAction<>& ca = clickActions[i];

			// A default-constructed ClickAction has no click; there is nothing to report for it
			if (ca.click != nullptr) {
				ca.click->ForEachButton(func);
			}

			for (size_t j = 0; j < ca.holdCount; j++) {
				if (ca.holdConditions[j]) {
					func(ca.holdConditions[j]);
				}
			}
		}

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
