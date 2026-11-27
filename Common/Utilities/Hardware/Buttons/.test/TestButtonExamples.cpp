#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Hardware/Buttons/Core.h>

#include <chrono>
#include <string>

// The examples of the Buttons wiki page, run on the host. The code between the markers is the code of the page. The host stands in for
// the RTOS: it supplies the tick and the GPIO level.

using namespace std::chrono_literals;


namespace {


int recordingStarts = 0;
int recordingStops = 0;


void StartRecording() {
	recordingStarts++;
}


void StopRecording() {
	recordingStops++;
}


// --- wiki: main loop (measured time) ---
void ProcessMeasured(Buttons::Core<8>& core, uint64 nowMs, uint64& lastMs) {
	core.ProcessEvents(std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(nowMs - lastMs)));
	lastMs = nowMs;
}
// --- end ---


void RunFor(Buttons::Core<8>& core, uint64& lastMs, uint64& nowMs, uint64 durationMs) {
	for (uint64 elapsed = 0; elapsed < durationMs; elapsed += 10) {
		nowMs += 10;
		ProcessMeasured(core, nowMs, lastMs);
	}
}


// Quick Start: three short presses of button A.
void QuickStartTriplePress() {
	int tripleCount = 0;

	// --- wiki: quick start ---
	AGPIO gpioA(37, true);  // active-low: pressed = pin low
	Buttons::Button btnA(gpioA);
	Buttons::ClickDefault clickA(btnA);

	Buttons::EventDefault triplePress({
		Buttons::MakeClickAction(clickA, Buttons::Action(Buttons::Action::ClickType::Short, 3))
	});
	triplePress.SetOnDoneHandle([&tripleCount]() {
		tripleCount++;  // triggered after 3 short presses
	});

	Buttons::Core<8> core;
	core.AddEvent(triplePress);
	core.EnableAll();
	// --- end ---

	uint64 lastMs = 0;
	uint64 nowMs = 0;
	for (int press = 0; press < 3; press++) {
		gpioA.SetRawLevel(false);
		RunFor(core, lastMs, nowMs, 100);
		gpioA.SetRawLevel(true);
		RunFor(core, lastMs, nowMs, 200);
	}
	// The triple press completes once.
	VHAL_CHECK_EQ(tripleCount, 1);
}


// Button: an active-low button is declared with inversion on the AGPIO.
void ActiveLowButtonDeclaredWithInversion() {
	AGPIO buttonA = { 37, true };  // active-low: pressed = pin low
	Buttons::Button btnA(buttonA);
	buttonA.SetRawLevel(false);
	for (std::chrono::milliseconds now{0}; now <= 100ms; now += 10ms) {
		btnA.Update(now);
	}
	// An active-low button is pressed with the pin low.
	VHAL_CHECK(btnA.IsPressed());
}


// Push-to-talk: press starts recording, release stops it.
void PushToTalkStartsOnPressStopsOnRelease() {
	// --- wiki: push-to-talk ---
	AGPIO voiceButton = { 37, true };          // GPIO37, active-low: pressed = pin low
	Buttons::Button voice(voiceButton);
	Buttons::ClickDefault voiceClick(voice);

	voiceClick.SetOnPushHandle([]() {
		StartRecording();                      // button pressed (after the debounce time)
	});
	voiceClick.SetOnPullHandle([]() {
		StopRecording();                       // button released (after the debounce time)
	});

	Buttons::Core<8> core;
	core.RegisterClick(voiceClick);            // no event needed
	// --- end ---

	int startsBefore = recordingStarts;
	int stopsBefore = recordingStops;
	uint64 lastMs = 0;
	uint64 nowMs = 0;

	voiceButton.SetRawLevel(false);  // press
	RunFor(core, lastMs, nowMs, 300);
	// The press starts recording and does not stop it.
	VHAL_CHECK(recordingStarts == startsBefore + 1 && recordingStops == stopsBefore);

	voiceButton.SetRawLevel(true);  // release
	RunFor(core, lastMs, nowMs, 300);
	// The release stops recording.
	VHAL_CHECK(recordingStarts == startsBefore + 1 && recordingStops == stopsBefore + 1);
}


} // namespace


int main() {
	QuickStartTriplePress();
	ActiveLowButtonDeclaredWithInversion();
	PushToTalkStartsOnPressStopsOnRelease();

	return VhalCheck::Finish("TestButtonExamples");
}
