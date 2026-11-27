#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Hardware/Buttons/Core.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

// Host tests of the buttons: the pin polarity (an active-low pin is pressed when low), the debounce, the click (push and pull
// handlers, the push-to-talk contact pattern), the event (hold, long hold, the sequence timeout), and the core's polling.

using namespace std::chrono_literals;


namespace {


constexpr std::chrono::milliseconds tickPeriod{10};


// Drives the raw pin so that the logical state (GetState) reads as requested.
// For an inverted (active-low) AGPIO a pressed button pulls the pin low.
void SetLogical(AGPIO& pin, bool pressed) {
	pin.SetRawLevel(pin.IsInversion() ? !pressed : pressed);
}


void Advance(Buttons::Core<8>& core, std::chrono::milliseconds duration) {
	for (std::chrono::milliseconds elapsed{0}; elapsed < duration; elapsed += tickPeriod) {
		core.ProcessEvents(tickPeriod);
	}
}


// Push-to-talk contact pattern: flicker at press and at release, then steady.
bool PushToTalkPressed(std::chrono::milliseconds now) {
	if (now < 40ms) {
		return (now / tickPeriod) % 2 == 0;
	}
	if (now < 200ms) {
		return true;
	}
	if (now < 240ms) {
		return (now / tickPeriod) % 2 == 1;
	}
	return false;
}


// Core::RegisterClick and RemoveClick are detected at build time, so that a missing API is reported as a failed check.
template<typename CoreType>
bool TryRegisterClick(CoreType& core, Buttons::ClickDefault& click) {
	if constexpr (requires (CoreType& target, Buttons::ClickDefault& item) { target.RegisterClick(item); }) {
		return core.RegisterClick(click);
	} else {
		(void)core;
		(void)click;
		return false;
	}
}


template<typename CoreType>
bool TryRemoveClick(CoreType& core, Buttons::ClickDefault& click) {
	if constexpr (requires (CoreType& target, Buttons::ClickDefault& item) { target.RemoveClick(item); }) {
		return core.RemoveClick(click);
	} else {
		(void)core;
		(void)click;
		return false;
	}
}


// Counts ticks from a raw level change until the debounced state flips. Used for timing checks that must not depend on which
// logical direction the pin is in.
std::size_t CountTicksUntilChanged(Buttons::Core<8>& core, Buttons::Button& button, AGPIO& pin, bool rawLevel) {
	pin.SetRawLevel(rawLevel);
	std::size_t ticks = 0;
	do {
		core.ProcessEvents(tickPeriod);
		ticks++;
	} while (!button.HasChanged() && ticks < 1000);
	return ticks;
}


std::size_t MeasureLatency(Buttons::Core<8>& core, Buttons::Button& button, AGPIO& pin, bool rawLevel) {
	pin.SetRawLevel(!rawLevel);
	Advance(core, 200ms);
	return CountTicksUntilChanged(core, button, pin, rawLevel);
}


// ---------------------------------------------------------------- Button


void ActiveLowPinLowIsPressed() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	pin.SetRawLevel(false);
	for (std::chrono::milliseconds now{0}; now <= 100ms; now += tickPeriod) {
		button.Update(now);
	}
	// A pin low on an active-low AGPIO is not reported as pressed.
	VHAL_CHECK(button.IsPressed());
}


void ActiveLowPinHighIsReleased() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	pin.SetRawLevel(true);
	for (std::chrono::milliseconds now{0}; now <= 100ms; now += tickPeriod) {
		button.Update(now);
	}
	// A pin high on an active-low AGPIO is not reported as pressed.
	VHAL_CHECK(!button.IsPressed());
}


void ActiveHighNonInvertedPinHighIsPressed() {
	AGPIO pin(37, false);
	Buttons::Button button(pin);
	pin.SetRawLevel(true);
	for (std::chrono::milliseconds now{0}; now <= 100ms; now += tickPeriod) {
		button.Update(now);
	}
	// A pin high on a non-inverted AGPIO is pressed.
	VHAL_CHECK(button.IsPressed());
}


void EdgesFollowLogicalState() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	std::vector<std::chrono::milliseconds> pressEdges;
	std::vector<std::chrono::milliseconds> releaseEdges;
	for (std::chrono::milliseconds now{0}; now <= 300ms; now += tickPeriod) {
		SetLogical(pin, now >= 110ms && now < 210ms);
		button.Update(now);
		if (button.WasPressed()) {
			pressEdges.push_back(now);
		}
		if (button.WasReleased()) {
			releaseEdges.push_back(now);
		}
	}
	// The press edge comes at 160 ms, once.
	VHAL_CHECK(pressEdges.size() == 1 && pressEdges[0] == 160ms);
	// The release edge comes at 260 ms, once.
	VHAL_CHECK(releaseEdges.size() == 1 && releaseEdges[0] == 260ms);
}


// Settle low, bounce from 100 ms to 160 ms, then hold high. Only the state change after the bounce is checked, and it must come
// exactly 50 ms after the last bounce. Written as "state changed" rather than press/release so it does not depend on polarity.
void BounceFilteredByDebounce() {
	AGPIO pin(37, false);
	Buttons::Button button(pin);
	std::vector<std::chrono::milliseconds> changes;
	for (std::chrono::milliseconds now{0}; now <= 300ms; now += tickPeriod) {
		bool level = false;
		if (now >= 100ms && now < 160ms) {
			level = (now / tickPeriod) % 2 == 0;
		} else if (now >= 160ms) {
			level = true;
		}
		pin.SetRawLevel(level);
		button.Update(now);
		if (now >= 100ms && button.HasChanged()) {
			changes.push_back(now);
		}
	}
	// The bounce is filtered: one state change, at 210 ms.
	VHAL_CHECK(changes.size() == 1 && changes[0] == 210ms);
}


void ZeroDebounceReactsSameTick() {
	AGPIO pin(37, false);
	Buttons::Button button(pin);
	button.SetDebounceTime(0ms);
	std::vector<std::chrono::milliseconds> changes;
	for (std::chrono::milliseconds now{0}; now <= 200ms; now += tickPeriod) {
		pin.SetRawLevel(now >= 100ms);
		button.Update(now);
		if (now >= 100ms && button.HasChanged()) {
			changes.push_back(now);
		}
	}
	// With 0 ms debounce the state change is seen on the tick it happens.
	VHAL_CHECK(changes.size() == 1 && changes[0] == 100ms);
}


// ---------------------------------------------------------------- Click


void EmptyCombinationNeverPressed() {
	Buttons::ClickDefault empty(std::initializer_list<std::reference_wrapper<Buttons::Button>>{});
	Buttons::EventDefault check({ Buttons::MakeClickAction(empty, Buttons::Action(Buttons::Action::ClickType::Short, 1)) });
	Buttons::Core<8> core;
	core.AddEvent(check);
	core.EnableAll();
	Advance(core, 100ms);
	// A click with no buttons is never pressed.
	VHAL_CHECK(!empty.IsPressed());
}


void StandalonePushPullWithoutEvent() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	int pushCount = 0;
	int pullCount = 0;
	click.SetOnPushHandle([&pushCount]() {
		pushCount++;
	});
	click.SetOnPullHandle([&pullCount]() {
		pullCount++;
	});
	Buttons::Core<8> core;
	if (!TryRegisterClick(core, click)) {
		// Core::RegisterClick is not available.
		VHAL_CHECK(false);
		return;
	}
	SetLogical(pin, true);
	Advance(core, 200ms);
	VHAL_CHECK_EQ(pushCount, 1);
	SetLogical(pin, false);
	Advance(core, 200ms);
	VHAL_CHECK_EQ(pullCount, 1);
}


void StandaloneRemoveStopsCallbacks() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	int pushCount = 0;
	click.SetOnPushHandle([&pushCount]() {
		pushCount++;
	});
	Buttons::Core<8> core;
	if (!TryRegisterClick(core, click)) {
		// Core::RegisterClick is not available.
		VHAL_CHECK(false);
		return;
	}
	if (!TryRemoveClick(core, click)) {
		// Core::RemoveClick is not available.
		VHAL_CHECK(false);
		return;
	}
	SetLogical(pin, true);
	Advance(core, 200ms);
	// The push handler does not fire after RemoveClick.
	VHAL_CHECK_EQ(pushCount, 0);
}


void PushToTalkBounceStandalone() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	int pushCount = 0;
	int pullCount = 0;
	click.SetOnPushHandle([&pushCount]() {
		pushCount++;
	});
	click.SetOnPullHandle([&pullCount]() {
		pullCount++;
	});
	Buttons::Core<8> core;
	if (!TryRegisterClick(core, click)) {
		// Core::RegisterClick is not available.
		VHAL_CHECK(false);
		return;
	}
	for (std::chrono::milliseconds now{0}; now <= 400ms; now += tickPeriod) {
		SetLogical(pin, PushToTalkPressed(now));
		core.ProcessEvents(tickPeriod);
	}
	// The push-to-talk start fires once, and the stop fires once.
	VHAL_CHECK_EQ(pushCount, 1);
	VHAL_CHECK_EQ(pullCount, 1);
}


void PushToTalkBounceViaEventWrapper() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	int pushCount = 0;
	int pullCount = 0;
	click.SetOnPushHandle([&pushCount]() {
		pushCount++;
	});
	click.SetOnPullHandle([&pullCount]() {
		pullCount++;
	});
	Buttons::EventDefault talk({ Buttons::MakeClickAction(click, Buttons::Action(Buttons::Action::ClickType::Infinitely, 1)) });
	Buttons::Core<8> core;
	core.AddEvent(talk);
	core.EnableAll();
	for (std::chrono::milliseconds now{0}; now <= 400ms; now += tickPeriod) {
		SetLogical(pin, PushToTalkPressed(now));
		core.ProcessEvents(tickPeriod);
	}
	// The push-to-talk start fires once, and the stop fires once.
	VHAL_CHECK_EQ(pushCount, 1);
	VHAL_CHECK_EQ(pullCount, 1);
}


// ---------------------------------------------------------------- Event


void InfinitelyFiresOnlyOnRelease() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	int doneCount = 0;
	Buttons::EventDefault hold({ Buttons::MakeClickAction(click, Buttons::Action(Buttons::Action::ClickType::Infinitely, 1)) });
	hold.SetOnDoneHandle([&doneCount]() {
		doneCount++;
	});
	Buttons::Core<8> core;
	core.AddEvent(hold);
	core.EnableAll();
	SetLogical(pin, true);
	Advance(core, 1000ms);
	// The infinite hold does not complete while the button is still held.
	VHAL_CHECK_EQ(doneCount, 0);
	SetLogical(pin, false);
	Advance(core, 200ms);
	// The infinite hold completes on release.
	VHAL_CHECK_EQ(doneCount, 1);
}


void InfinitelyHoldBeyondSequenceTimeout() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	int doneCount = 0;
	int failedCount = 0;
	Buttons::EventDefault hold({ Buttons::MakeClickAction(click, Buttons::Action(Buttons::Action::ClickType::Infinitely, 1)) });
	hold.SetOnDoneHandle([&doneCount]() {
		doneCount++;
	});
	hold.SetOnFailedHandle([&failedCount]() {
		failedCount++;
	});
	Buttons::Core<8> core;
	core.AddEvent(hold);
	core.EnableAll();
	SetLogical(pin, true);
	Advance(core, 8000ms);
	SetLogical(pin, false);
	Advance(core, 200ms);
	// The hold does not time out while the button is held.
	VHAL_CHECK_EQ(failedCount, 0);
	// An 8 s infinite hold completes on release.
	VHAL_CHECK_EQ(doneCount, 1);
}


void VeryLongHoldBeyondSequenceTimeout() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	int doneCount = 0;
	int failedCount = 0;
	Buttons::EventDefault hold({ Buttons::MakeClickAction(click, Buttons::Action(Buttons::Action::ClickType::VeryLong, 1)) });
	hold.SetOnDoneHandle([&doneCount]() {
		doneCount++;
	});
	hold.SetOnFailedHandle([&failedCount]() {
		failedCount++;
	});
	Buttons::Core<8> core;
	core.AddEvent(hold);
	core.EnableAll();
	SetLogical(pin, true);
	Advance(core, 6000ms);
	SetLogical(pin, false);
	Advance(core, 200ms);
	// The very long press does not time out while it is held.
	VHAL_CHECK_EQ(failedCount, 0);
	// A 6 s very long press completes on release.
	VHAL_CHECK_EQ(doneCount, 1);
}


void TimeoutStillAppliesBetweenActions() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	int doneCount = 0;
	int failedCount = 0;
	Buttons::EventDefault sequence({
		Buttons::MakeClickAction(click, Buttons::Action(Buttons::Action::ClickType::Short, 1)),
		Buttons::MakeClickAction(click, Buttons::Action(Buttons::Action::ClickType::Short, 1))
	});
	sequence.SetOnDoneHandle([&doneCount]() {
		doneCount++;
	});
	sequence.SetOnFailedHandle([&failedCount]() {
		failedCount++;
	});
	Buttons::Core<8> core;
	core.AddEvent(sequence);
	core.EnableAll();
	SetLogical(pin, true);
	Advance(core, 100ms);
	SetLogical(pin, false);
	Advance(core, 100ms);
	Advance(core, 6000ms);
	// An idle gap between the sequence steps times out once.
	VHAL_CHECK_EQ(failedCount, 1);
	// The sequence does not complete after a timed-out gap.
	VHAL_CHECK_EQ(doneCount, 0);
}


void NullClickActionDoesNotCrash() {
	Buttons::EventDefault::ClickAction<> nullAction;
	Buttons::EventDefault event(nullAction);
	Buttons::Core<8> core;
	// AddEvent accepts an event with a null click.
	VHAL_CHECK(core.AddEvent(event));
	core.EnableAll();
	Advance(core, 50ms);
}


// ---------------------------------------------------------------- Core


void DuplicateAddEventRejected() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	int doneCount = 0;
	Buttons::EventDefault triple({ Buttons::MakeClickAction(click, Buttons::Action(Buttons::Action::ClickType::Short, 3)) });
	triple.SetOnDoneHandle([&doneCount]() {
		doneCount++;
	});
	Buttons::Core<8> core;
	// The first AddEvent succeeds.
	VHAL_CHECK(core.AddEvent(triple));
	// The same event is not registered twice.
	VHAL_CHECK(!core.AddEvent(triple));
	VHAL_CHECK_EQ(core.GetEventCount(), 1);
	core.EnableAll();
	for (int press = 1; press <= 3; press++) {
		SetLogical(pin, true);
		Advance(core, 100ms);
		SetLogical(pin, false);
		Advance(core, 200ms);
		if (press == 2) {
			// The triple click does not complete after 2 presses.
			VHAL_CHECK_EQ(doneCount, 0);
		}
	}
	// The triple click completes after 3 presses.
	VHAL_CHECK_EQ(doneCount, 1);
}


void ProcessEventsNoDriftAfterLongRun() {
	AGPIO pin(37, false);
	Buttons::Button button(pin);
	Buttons::Core<8> core;
	core.RegisterButton(button);
	std::size_t risingBefore = MeasureLatency(core, button, pin, true);
	std::size_t fallingBefore = MeasureLatency(core, button, pin, false);
	Advance(core, 1000000ms);
	std::size_t risingAfter = MeasureLatency(core, button, pin, true);
	std::size_t fallingAfter = MeasureLatency(core, button, pin, false);
	// The debounce completes in both directions.
	VHAL_CHECK(risingBefore > 0 && fallingBefore > 0);
	// The debounce latency does not change after a long run.
	VHAL_CHECK_EQ(risingBefore, risingAfter);
	VHAL_CHECK_EQ(fallingBefore, fallingAfter);
}


} // namespace


int main() {
	ActiveLowPinLowIsPressed();
	ActiveLowPinHighIsReleased();
	ActiveHighNonInvertedPinHighIsPressed();
	EdgesFollowLogicalState();
	BounceFilteredByDebounce();
	ZeroDebounceReactsSameTick();
	EmptyCombinationNeverPressed();
	StandalonePushPullWithoutEvent();
	StandaloneRemoveStopsCallbacks();
	PushToTalkBounceStandalone();
	PushToTalkBounceViaEventWrapper();
	InfinitelyFiresOnlyOnRelease();
	InfinitelyHoldBeyondSequenceTimeout();
	VeryLongHoldBeyondSequenceTimeout();
	TimeoutStillAppliesBetweenActions();
	NullClickActionDoesNotCrash();
	DuplicateAddEventRejected();
	ProcessEventsNoDriftAfterLongRun();

	return VhalCheck::Finish("TestButtons");
}
