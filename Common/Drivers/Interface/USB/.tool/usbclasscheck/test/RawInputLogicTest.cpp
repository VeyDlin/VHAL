#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "RawInputLogic.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


// The events of one function key: a make, then a break.
void AddKey(std::vector<KeyEvent>& events, std::uint16_t virtualKey) {
	KeyEvent make;
	make.virtualKey = virtualKey;
	make.breakCode = false;
	events.push_back(make);

	KeyEvent release;
	release.virtualKey = virtualKey;
	release.breakCode = true;
	events.push_back(release);
}


std::vector<KeyEvent> AllFunctionKeys() {
	std::vector<KeyEvent> events;
	for (std::uint16_t index = 0; index < FunctionKeyCount; index++) {
		AddKey(events, static_cast<std::uint16_t>(FirstFunctionKeyVirtual + index));
	}

	return events;
}


void CheckKeyNames() {
	Expect(FunctionKeyName(0) == "f13", "index 0 is f13");
	Expect(FunctionKeyName(11) == "f24", "index 11 is f24");
	Expect(FirstFunctionKeyVirtual == 0x7C, "F13 is virtual key 0x7C");
}


void CheckKeySequence() {
	std::string detail;
	std::vector<KeyEvent> events = AllFunctionKeys();
	Expect(CheckFunctionKeys(events, detail), "F13 to F24, make and break in order, passes");

	std::vector<KeyEvent> missing = events;
	missing.pop_back();
	Expect(!CheckFunctionKeys(missing, detail), "a missing break is a failure");
	Expect(detail.find("expected 24") != std::string::npos, "the count of events is named");

	std::vector<KeyEvent> swapped = events;
	std::swap(swapped[2], swapped[4]);
	Expect(!CheckFunctionKeys(swapped, detail), "keys out of order are a failure");
	Expect(detail.find("event 3") != std::string::npos, "the first event out of place is named");

	std::vector<KeyEvent> wrongBreak = events;
	wrongBreak[1].breakCode = false;
	Expect(!CheckFunctionKeys(wrongBreak, detail), "a make in place of a break is a failure");

	std::vector<KeyEvent> none;
	Expect(!CheckFunctionKeys(none, detail), "no events is a failure");
	Expect(detail.find("0 key events") != std::string::npos, "no events is named");
}


void CheckMotion() {
	std::vector<MouseEvent> legs;
	MouseEvent first;
	first.dx = 25;
	first.dy = -10;
	legs.push_back(first);
	MouseEvent second;
	second.dx = -25;
	second.dy = 10;
	legs.push_back(second);
	MouseEvent up;
	up.wheel = 1;
	legs.push_back(up);
	MouseEvent down;
	down.wheel = -1;
	legs.push_back(down);

	std::string detail;
	std::vector<MouseEvent> observed = legs;
	Expect(CheckMotionReturned(legs, observed, detail), "the legs coming back as sent pass");

	// The same movement split into several reports still counts as sent.
	std::vector<MouseEvent> split;
	for (int step = 0; step < 5; step++) {
		MouseEvent part;
		part.dx = 5;
		part.dy = -2;
		split.push_back(part);
	}

	for (int step = 0; step < 5; step++) {
		MouseEvent part;
		part.dx = -5;
		part.dy = 2;
		split.push_back(part);
	}

	MouseEvent wheelUp;
	wheelUp.wheel = 120;
	split.push_back(wheelUp);
	MouseEvent wheelDown;
	wheelDown.wheel = -120;
	split.push_back(wheelDown);
	Expect(CheckMotionReturned(legs, split, detail), "a movement split in reports still passes");

	std::vector<MouseEvent> lost = observed;
	lost.erase(lost.begin());
	Expect(!CheckMotionReturned(legs, lost, detail), "a lost movement leg is a failure");
	Expect(detail.find("x ") != std::string::npos, "the totals are reported");

	std::vector<MouseEvent> drifted = observed;
	drifted[0].dx = 26;
	Expect(!CheckMotionReturned(legs, drifted, detail), "a movement that does not match is a failure");

	std::vector<MouseEvent> noWheel = observed;
	noWheel.pop_back();
	noWheel.pop_back();
	Expect(!CheckMotionReturned(legs, noWheel, detail), "a missing wheel is a failure");

	std::vector<MouseEvent> oneWheelWay = observed;
	oneWheelWay.push_back(up);
	Expect(!CheckMotionReturned(legs, oneWheelWay, detail), "a wheel that does not net to zero is a failure");

	std::vector<MouseEvent> none;
	Expect(!CheckMotionReturned(legs, none, detail), "no events is a failure");
}


}


int main() {
	CheckKeyNames();
	CheckKeySequence();
	CheckMotion();

	std::printf("usbclasscheck raw input logic test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
