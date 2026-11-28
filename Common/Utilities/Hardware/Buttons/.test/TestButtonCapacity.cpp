#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Hardware/Buttons/Core.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Host tests of the fixed capacities of the buttons: a click holds its maximum of buttons, an event its maximum of steps, the core its
// maximum of events, clicks and buttons. A rejected add leaves the object as it was, and an event with a step that overflows is refused.

using namespace std::chrono_literals;


namespace {


using ShortAction = Buttons::Action;


ShortAction ShortOnce() {
	return ShortAction(ShortAction::ClickType::Short, 1);
}


// --------------------------------------------------------------- Action


void ZeroCountRejectedBySetter() {
	Buttons::Action action;
	// SetClicksCount(0) is refused.
	VHAL_CHECK(!action.SetClicksCount(0));
	VHAL_CHECK_EQ(action.GetClicksCount(), 1);
	// SetClicksCount(2) is accepted.
	VHAL_CHECK(action.SetClicksCount(2));
	VHAL_CHECK_EQ(action.GetClicksCount(), 2);
}


void ZeroCountActionRejectedByCore() {
	AGPIO pin(37, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	Buttons::Action zero(Buttons::Action::ClickType::Short, 0);
	// An action with count 0 is not valid.
	VHAL_CHECK(!zero.IsValid());
	Buttons::EventDefault event(Buttons::MakeClickAction(click, zero));
	VHAL_CHECK(!event.IsComplete());
	Buttons::Core<8> core;
	// AddEvent refuses a count-0 action.
	VHAL_CHECK(!core.AddEvent(event));
	VHAL_CHECK_EQ(core.GetEventCount(), 0);
}


// --------------------------------------------------------------- Click


void ClickAddButtonBeyondCapacity() {
	AGPIO pin1(1, true);
	AGPIO pin2(2, true);
	AGPIO pin3(3, true);
	Buttons::Button button1(pin1);
	Buttons::Button button2(pin2);
	Buttons::Button button3(pin3);
	Buttons::Click<2> click(button1);
	// The second button is accepted while a slot is free.
	VHAL_CHECK(click.AddButton(button2));
	// The third button is refused beyond MaxButtons 2.
	VHAL_CHECK(!click.AddButton(button3));
	int count = 0;
	click.ForEachButton([&count](Buttons::Button* button) {
		(void)button;
		count++;
	});
	VHAL_CHECK_EQ(count, 2);
	// A refused AddButton does not mark the click incomplete.
	VHAL_CHECK(click.IsComplete());
}


void ClickConstructorOverflowRejectedByCore() {
	AGPIO pins[5] = { AGPIO(1, true), AGPIO(2, true), AGPIO(3, true), AGPIO(4, true), AGPIO(5, true) };
	Buttons::Button b1(pins[0]);
	Buttons::Button b2(pins[1]);
	Buttons::Button b3(pins[2]);
	Buttons::Button b4(pins[3]);
	Buttons::Button b5(pins[4]);
	Buttons::ClickDefault overfull({ b1, b2, b3, b4, b5 });
	// A click with 5 buttons in a 4-button click is not complete.
	VHAL_CHECK(!overfull.IsComplete());
	Buttons::Core<8> core;
	// RegisterClick refuses an incomplete click.
	VHAL_CHECK(!core.RegisterClick(overfull));

	Buttons::ClickDefault fine({ b1, b2 });
	VHAL_CHECK(fine.IsComplete());
	// The core is still usable after a rejected click.
	VHAL_CHECK(core.RegisterClick(fine));
}


// --------------------------------------------------------------- Event


void EventAddStepBeyondCapacity() {
	AGPIO pin(1, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	Buttons::EventDefault event(Buttons::MakeClickAction(click, ShortOnce()));
	for (int added = 1; added < 8; added++) {
		Buttons::EventDefault::ClickAction<> step = Buttons::MakeClickAction(click, ShortOnce());
		// AddStep accepts steps up to 8 in total.
		VHAL_CHECK(event.AddStep(step));
	}
	Buttons::EventDefault::ClickAction<> ninth = Buttons::MakeClickAction(click, ShortOnce());
	// AddStep refuses a 9th step beyond MaxSequences 8.
	VHAL_CHECK(!event.AddStep(ninth));
	// A refused AddStep does not mark the event incomplete.
	VHAL_CHECK(event.IsComplete());
	Buttons::Core<8> core;
	// An event with 8 steps is accepted by AddEvent.
	VHAL_CHECK(core.AddEvent(event));
}


void EventConstructorOverflowRejectedByCore() {
	AGPIO pin(1, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	Buttons::EventDefault nine({
		Buttons::MakeClickAction(click, ShortOnce()),
		Buttons::MakeClickAction(click, ShortOnce()),
		Buttons::MakeClickAction(click, ShortOnce()),
		Buttons::MakeClickAction(click, ShortOnce()),
		Buttons::MakeClickAction(click, ShortOnce()),
		Buttons::MakeClickAction(click, ShortOnce()),
		Buttons::MakeClickAction(click, ShortOnce()),
		Buttons::MakeClickAction(click, ShortOnce()),
		Buttons::MakeClickAction(click, ShortOnce())
	});
	// An event with 9 steps is not complete.
	VHAL_CHECK(!nine.IsComplete());
	Buttons::Core<8> core;
	// AddEvent refuses an event with 9 steps.
	VHAL_CHECK(!core.AddEvent(nine));
	VHAL_CHECK_EQ(core.GetEventCount(), 0);
}


void EventWhileHoldingLimitAllOrNothing() {
	AGPIO pins[5] = { AGPIO(1, true), AGPIO(2, true), AGPIO(3, true), AGPIO(4, true), AGPIO(5, true) };
	Buttons::Button b1(pins[0]);
	Buttons::Button b2(pins[1]);
	Buttons::Button b3(pins[2]);
	Buttons::Button b4(pins[3]);
	Buttons::Button b5(pins[4]);
	Buttons::ClickDefault click(b1);
	Buttons::EventDefault event(Buttons::MakeClickAction(click, ShortOnce()));
	// The first global hold is accepted.
	VHAL_CHECK(event.WhileHolding(b1));
	// A list of 4 holds is refused with 3 slots left.
	VHAL_CHECK(!event.WhileHolding({ b2, b3, b4, b5 }));
	// The refused list added nothing, so exactly 3 more fit.
	VHAL_CHECK(event.WhileHolding({ b2, b3, b4 }));
	// A fifth global hold is refused beyond the limit of 4.
	VHAL_CHECK(!event.WhileHolding(b5));
}


void ClickActionHoldOverflowRejectedByCore() {
	AGPIO pins[5] = { AGPIO(1, true), AGPIO(2, true), AGPIO(3, true), AGPIO(4, true), AGPIO(5, true) };
	Buttons::Button b1(pins[0]);
	Buttons::Button b2(pins[1]);
	Buttons::Button b3(pins[2]);
	Buttons::Button b4(pins[3]);
	Buttons::Button b5(pins[4]);
	Buttons::ClickDefault click(b1);

	Buttons::EventDefault::ClickAction<> full = Buttons::MakeClickActionWithHold(click, ShortOnce(), { b1, b2, b3, b4 });
	// A step with 4 hold buttons is complete.
	VHAL_CHECK(full.IsComplete());
	// A fifth per-action hold is refused.
	VHAL_CHECK(!full.WhileHolding(b5));
	VHAL_CHECK_EQ(full.holdCount, 4);

	Buttons::EventDefault::ClickAction<> overfull = Buttons::MakeClickActionWithHold(click, ShortOnce(), { b1, b2, b3, b4, b5 });
	// A step with 5 hold buttons is not complete.
	VHAL_CHECK(!overfull.IsComplete());
	Buttons::Core<8> core;
	Buttons::EventDefault event(overfull);
	// AddEvent refuses a step whose holds overflowed.
	VHAL_CHECK(!core.AddEvent(event));
}


// --------------------------------------------------------------- Core


void CoreAddEventRejectsWhenEventListFull() {
	AGPIO pin(1, true);
	Buttons::Button button(pin);
	Buttons::ClickDefault click(button);
	Buttons::EventDefault first(Buttons::MakeClickAction(click, ShortOnce()));
	Buttons::EventDefault second(Buttons::MakeClickAction(click, ShortOnce()));
	Buttons::EventDefault third(Buttons::MakeClickAction(click, ShortOnce()));
	Buttons::Core<2> core;
	VHAL_CHECK(core.AddEvent(first));
	VHAL_CHECK(core.AddEvent(second));
	// The third event is refused with MaxEvents 2.
	VHAL_CHECK(!core.AddEvent(third));
	VHAL_CHECK_EQ(core.GetEventCount(), 2);
}


void CoreRegisterClickRejectsWhenClickListFull() {
	AGPIO pins[3] = { AGPIO(1, true), AGPIO(2, true), AGPIO(3, true) };
	Buttons::Button b1(pins[0]);
	Buttons::Button b2(pins[1]);
	Buttons::Button b3(pins[2]);
	Buttons::ClickDefault c1(b1);
	Buttons::ClickDefault c2(b2);
	Buttons::ClickDefault c3(b3);
	Buttons::Core<8, 2> core;
	VHAL_CHECK(core.RegisterClick(c1));
	VHAL_CHECK(core.RegisterClick(c2));
	// The third click is refused with MaxClicks 2.
	VHAL_CHECK(!core.RegisterClick(c3));
	// A duplicate click is refused.
	VHAL_CHECK(!core.RegisterClick(c1));
	// A click can be removed while the list is full.
	VHAL_CHECK(core.RemoveClick(c2));
	// A click is accepted once a slot is freed.
	VHAL_CHECK(core.RegisterClick(c3));
}


void CoreRejectsWhenButtonListFull() {
	std::vector<std::unique_ptr<AGPIO>> pins;
	std::vector<std::unique_ptr<Buttons::Button>> buttons;
	for (int i = 0; i < 33; i++) {
		pins.push_back(std::make_unique<AGPIO>(uint8(i + 1), true));
		buttons.push_back(std::make_unique<Buttons::Button>(*pins.back()));
	}

	Buttons::Core<8> core;
	for (int i = 0; i < 32; i++) {
		// The buttons 1 to 32 are accepted.
		VHAL_CHECK(core.RegisterButton(*buttons[i]));
	}
	// The 33rd button is refused with a 32-button list.
	VHAL_CHECK(!core.RegisterButton(*buttons[32]));

	Buttons::ClickDefault overflowClick(*buttons[32]);
	Buttons::EventDefault overflowEvent(Buttons::MakeClickAction(overflowClick, ShortOnce()));
	// AddEvent refuses an event with a 33rd button.
	VHAL_CHECK(!core.AddEvent(overflowEvent));
	VHAL_CHECK_EQ(core.GetEventCount(), 0);

	Buttons::ClickDefault overflowStandalone(*buttons[32]);
	// RegisterClick refuses a 33rd button.
	VHAL_CHECK(!core.RegisterClick(overflowStandalone));

	Buttons::ClickDefault knownClick(*buttons[0]);
	Buttons::EventDefault knownEvent(Buttons::MakeClickAction(knownClick, ShortOnce()));
	// An event using only registered buttons is accepted.
	VHAL_CHECK(core.AddEvent(knownEvent));
}


void CoreProcessEventsUpdatesEveryClickAtMaximumLoad() {
	// MaxEvents 1, MaxClicks 2: the dedupe bound is 2 + 1 * 8 = 10 clicks, which is exactly what is used here.
	std::vector<std::unique_ptr<AGPIO>> pins;
	std::vector<std::unique_ptr<Buttons::Button>> buttons;
	std::vector<std::unique_ptr<Buttons::ClickDefault>> clicks;
	for (int i = 0; i < 10; i++) {
		pins.push_back(std::make_unique<AGPIO>(uint8(i + 1), true));
		buttons.push_back(std::make_unique<Buttons::Button>(*pins.back()));
		clicks.push_back(std::make_unique<Buttons::ClickDefault>(*buttons.back()));
	}

	Buttons::EventDefault event({
		Buttons::MakeClickAction(*clicks[0], ShortOnce()),
		Buttons::MakeClickAction(*clicks[1], ShortOnce()),
		Buttons::MakeClickAction(*clicks[2], ShortOnce()),
		Buttons::MakeClickAction(*clicks[3], ShortOnce()),
		Buttons::MakeClickAction(*clicks[4], ShortOnce()),
		Buttons::MakeClickAction(*clicks[5], ShortOnce()),
		Buttons::MakeClickAction(*clicks[6], ShortOnce()),
		Buttons::MakeClickAction(*clicks[7], ShortOnce())
	});
	Buttons::Core<1, 2> core;
	// An event with 8 steps is accepted.
	VHAL_CHECK(core.AddEvent(event));
	// Standalone clicks 9 and 10 are accepted.
	VHAL_CHECK(core.RegisterClick(*clicks[8]));
	VHAL_CHECK(core.RegisterClick(*clicks[9]));
	core.EnableAll();

	for (std::unique_ptr<AGPIO>& pin : pins) {
		pin->SetRawLevel(false);  // every active-low button pressed
	}
	bool allUpdated = true;
	for (int tick = 0; tick < 10; tick++) {
		allUpdated = core.ProcessEvents(10ms) && allUpdated;
	}
	// ProcessEvents records every click at maximum load.
	VHAL_CHECK(allUpdated);
	for (int i = 0; i < 10; i++) {
		// Every click is updated at maximum load.
		VHAL_CHECK(clicks[i]->IsPressed());
	}
}


} // namespace


int main() {
	ZeroCountRejectedBySetter();
	ZeroCountActionRejectedByCore();
	ClickAddButtonBeyondCapacity();
	ClickConstructorOverflowRejectedByCore();
	EventAddStepBeyondCapacity();
	EventConstructorOverflowRejectedByCore();
	EventWhileHoldingLimitAllOrNothing();
	ClickActionHoldOverflowRejectedByCore();
	CoreAddEventRejectsWhenEventListFull();
	CoreRegisterClickRejectsWhenClickListFull();
	CoreRejectsWhenButtonListFull();
	CoreProcessEventsUpdatesEveryClickAtMaximumLoad();

	return VhalCheck::Finish("TestButtonCapacity");
}
