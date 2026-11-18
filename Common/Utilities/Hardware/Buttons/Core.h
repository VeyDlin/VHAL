#pragma once
#include <VHAL.h>
#include "Action.h"
#include "Button.h"
#include "Click.h"
#include "Event.h"
#include <array>
#include <chrono>
#include <functional>
#include <initializer_list>

namespace Buttons {


template<size_t MaxEvents = 16, size_t MaxClicks = 8>
class Core {
public:
	// Timing configuration. Applied to buttons and events on registration, and to
	// already registered ones by UpdateTimeSettings().
	std::chrono::milliseconds debounceTime{50};          // Contact bounce filter duration
	std::chrono::milliseconds shortClickThreshold{500};  // Click < 500ms = short
	std::chrono::milliseconds longClickThreshold{2000};  // Click >= 2000ms = very long
	std::chrono::milliseconds sequenceTimeout{5000};     // Max time between sequence actions
	std::chrono::milliseconds multiClickGap{300};        // Max gap between multi-clicks

private:
	// Each event holds up to MaxSequences clicks (EventDefault is Event<8>), so this bounds the distinct clicks
	using UpdatedClicks = std::array<ClickDefault*, MaxClicks + MaxEvents * 8>;

	std::array<EventDefault*, MaxEvents> events;
	std::array<ClickDefault*, MaxClicks> clicks;  // Clicks registered without an event
	std::array<Button*, 32> buttons;              // All unique buttons for update
	size_t eventCount = 0;
	size_t clickCount = 0;
	size_t buttonCount = 0;
	std::chrono::milliseconds lastUpdateTime{0};

public:
	Core() {
		events.fill(nullptr);
		clicks.fill(nullptr);
		buttons.fill(nullptr);
	}

	// Returns false and leaves the core unchanged when the event list or the button list is
	// full, when the event is already registered, or when the event is not complete.
	bool AddEvent(EventDefault& event) {
		if (eventCount >= MaxEvents) {
			return false;
		}
		if (!event.IsComplete()) {
			return false;
		}
		// Registering the same event twice would update it twice per tick and count each release twice
		for (size_t i = 0; i < eventCount; i++) {
			if (events[i] == &event) {
				return false;
			}
		}
		if (!HasRoomForButtons(event)) {
			return false;
		}

		events[eventCount++] = &event;
		event.SetTimeout(sequenceTimeout);
		event.SetTimeThresholds(shortClickThreshold, longClickThreshold);
		event.SetMultiClickGap(multiClickGap);

		CollectButtons(event);

		return true;
	}

	bool RemoveEvent(EventDefault& event) {
		for (size_t i = 0; i < eventCount; i++) {
			if (events[i] == &event) {
				// Shift remaining events
				for (size_t j = i; j + 1 < eventCount; j++) {
					events[j] = events[j + 1];
				}
				events[--eventCount] = nullptr;
				return true;
			}
		}
		return false;
	}

	// Registers a click that is not part of an event, so its push/pull handlers are called
	// on every tick. Such clicks are not affected by EnableAll() or DisableAll().
	// Returns false and leaves the core unchanged when the click list or the button list is
	// full, when the click is already registered, or when the click is not complete.
	bool RegisterClick(ClickDefault& click) {
		if (clickCount >= MaxClicks) {
			return false;
		}
		if (!click.IsComplete()) {
			return false;
		}
		for (size_t i = 0; i < clickCount; i++) {
			if (clicks[i] == &click) {
				return false;
			}
		}
		if (!HasRoomForButtons(click)) {
			return false;
		}

		clicks[clickCount++] = &click;
		CollectButtons(click);
		return true;
	}

	bool RemoveClick(ClickDefault& click) {
		for (size_t i = 0; i < clickCount; i++) {
			if (clicks[i] == &click) {
				for (size_t j = i; j + 1 < clickCount; j++) {
					clicks[j] = clicks[j + 1];
				}
				clicks[--clickCount] = nullptr;
				return true;
			}
		}
		return false;
	}

	void EnableAll() {
		for (size_t i = 0; i < eventCount; i++) {
			if (events[i]) {
				events[i]->Enable();
			}
		}
	}

	void DisableAll() {
		for (size_t i = 0; i < eventCount; i++) {
			if (events[i]) {
				events[i]->Disable();
			}
		}
	}

	// deltaTime is the time elapsed since the previous call and must not be negative.
	// The time base is the sum of these deltas, so the caller's measurement sets the accuracy.
	// Returns false if a click could not be recorded for the tick; that cannot happen with the
	// template limits, because the bound on UpdatedClicks is the largest possible number of clicks.
	bool ProcessEvents(std::chrono::milliseconds deltaTime) {
		std::chrono::milliseconds currentTime = lastUpdateTime + deltaTime;
		bool allClicksUpdated = true;

		// Update all buttons with debounce filtering
		for (size_t i = 0; i < buttonCount; i++) {
			if (buttons[i]) {
				buttons[i]->Update(currentTime);
			}
		}

		// A click can belong to several events, so it is updated at most once per tick
		UpdatedClicks updatedClicks = {};
		size_t updatedCount = 0;
		for (size_t i = 0; i < clickCount; i++) {
			if (!UpdateClickOnce(clicks[i], currentTime, updatedClicks, updatedCount)) {
				allClicksUpdated = false;
			}
		}
		for (size_t index = 0; index < eventCount; index++) {
			if (events[index] == nullptr || !events[index]->IsEnabled()) {
				continue;
			}
			events[index]->ForEachClick([&](ClickDefault* click) {
				if (!UpdateClickOnce(click, currentTime, updatedClicks, updatedCount)) {
					allClicksUpdated = false;
				}
			});
		}

		for (size_t i = 0; i < eventCount; i++) {
			if (events[i] && events[i]->IsEnabled()) {
				events[i]->Update(currentTime, deltaTime);
			}
		}

		lastUpdateTime = currentTime;
		return allClicksUpdated;
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

	// Manual button registration for update loop. Returns false when the button list is full.
	bool RegisterButton(Button& button) {
		return AddButton(&button);
	}

private:
	// Registers the buttons of an event or click. Callers check HasRoomForButtons first.
	template<typename SourceType>
	void CollectButtons(SourceType& source) {
		source.ForEachButton([this](Button* button) {
			AddButton(button);
		});
	}

	// True when every button used by the source is already registered or still fits in the list.
	template<typename SourceType>
	bool HasRoomForButtons(SourceType& source) {
		std::array<Button*, 32> pending = {};
		size_t pendingCount = 0;
		bool fits = true;
		source.ForEachButton([&](Button* button) {
			if (!fits || IsRegistered(button)) {
				return;
			}
			for (size_t i = 0; i < pendingCount; i++) {
				if (pending[i] == button) {
					return;
				}
			}
			if (buttonCount + pendingCount >= buttons.size()) {
				fits = false;
				return;
			}
			pending[pendingCount++] = button;
		});
		return fits;
	}

	bool IsRegistered(Button* button) const {
		for (size_t i = 0; i < buttonCount; i++) {
			if (buttons[i] == button) {
				return true;
			}
		}
		return false;
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
		if (IsRegistered(button)) {
			return true;
		}
		if (buttonCount < buttons.size()) {
			buttons[buttonCount++] = button;
			button->SetDebounceTime(debounceTime);
			return true;
		}
		return false;
	}

	bool UpdateClickOnce(
		ClickDefault* click,
		std::chrono::milliseconds currentTime,
		UpdatedClicks& updatedClicks,
		size_t& updatedCount)
	{
		for (size_t previous = 0; previous < updatedCount; previous++) {
			if (updatedClicks[previous] == click) {
				return true;
			}
		}
		if (updatedCount >= updatedClicks.size()) {
			return false;
		}
		click->Update(currentTime);
		updatedClicks[updatedCount++] = click;
		return true;
	}
};

// Helper functions for creating ClickActions with hold conditions.
// Hold buttons that do not fit are reported by the returned action: IsComplete() is false.
template<size_t ClickMaxButtons = 4>
static EventDefault::ClickAction<> MakeClickAction(ClickDefault& click, const Action& action) {
	return EventDefault::ClickAction<>(click, action);
}

template<size_t ClickMaxButtons = 4>
static EventDefault::ClickAction<> MakeClickActionWithHold(
	ClickDefault& click,
	const Action& action,
	std::initializer_list<std::reference_wrapper<Button>> holdButtons)
{
	EventDefault::ClickAction<> ca = EventDefault::ClickAction<>(click, action);
	if (!ca.WhileHolding(holdButtons)) {
		ca.holdsAccepted = false;
	}
	return ca;
}

}
