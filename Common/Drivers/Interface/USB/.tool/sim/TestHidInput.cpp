#include <VHAL.h>
#include "HidTestDevices.h"
#include "SimAssert.h"
#include <Drivers/Interface/USB/Class/HID/HidKeyboard.h>
#include <Drivers/Interface/USB/Class/HID/HidMouse.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <span>
#include <thread>
#include <vector>


// The keyboard and the mouse on top of the HID function, through the simulated host. The reports are checked byte for byte, in order,
// as the host reads them. Each case states the queue depth it needs: a small queue shows the refusals (busy) and a large one the
// rollover and the key limit.


namespace {


using KeyboardDevice = HidTest::HidDevice<HidTest::KeyboardUsb, HidTest::KeyboardReport, 40>;
using KeyboardOutDevice = HidTest::HidDevice<HidTest::KeyboardOutUsb, HidTest::KeyboardReport, 8>;
using SmallKeyboardDevice = HidTest::HidDevice<HidTest::KeyboardUsb, HidTest::KeyboardReport, 2>;
using MouseDevice = HidTest::HidDevice<HidTest::MouseUsb, HidTest::MouseReport, 8>;
using SmallMouseDevice = HidTest::HidDevice<HidTest::MouseUsb, HidTest::MouseReport, 2>;


std::vector<uint8> Bytes(std::initializer_list<uint8> values) {
	return std::vector<uint8>(values);
}


// Reads what the host can take now. Process() starts the first queued report; the completions arm the rest.
template<typename TDevice>
std::vector<std::vector<uint8>> Flush(TDevice& device) {
	device.device.Process();
	return HidTest::DrainIn(device.sim, 0x81);
}


// The LED delegate of the keyboard: records every value it is called with.
std::vector<uint8> ledValues;


void OnLeds(uint8 leds) {
	ledValues.push_back(leds);
}


void CheckPressReleaseModifiers() {
	KeyboardDevice device;
	HidKeyboard<40> keyboard(device.function);
	device.Configure();

	SIM_EXPECT_EQ(keyboard.Press(0x04), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Press(0x04), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Press(0xE1), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Release(0x04), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Release(0x04), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.ReleaseAll(), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.ReleaseAll(), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.SetModifiers(0x40), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.SetModifiers(0x40), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Modifiers(), 0x40);

	// Each change sends one report: the repeats of a held key and a released key send nothing.
	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
	}, "press, release and modifiers", __LINE__);
}


// Seven held keys: the report is ErrorRollOver (every key slot 0x01) until a key is released, then the six that are left.
void CheckRollover() {
	KeyboardDevice device;
	HidKeyboard<40> keyboard(device.function);
	device.Configure();

	for (uint8 key = 0x04; key <= 0x09; key++) {
		SIM_EXPECT_EQ(keyboard.Press(key), ResultStatus::ok);
	}

	SIM_EXPECT_EQ(keyboard.Press(0x0A), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Release(0x04), ResultStatus::ok);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x04, 0x05, 0x06, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00 }),
		Bytes({ 0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 }),
		Bytes({ 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 }),
		Bytes({ 0x00, 0x00, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A })
	}, "rollover", __LINE__);
}


// Tap is a press and a release: two reports, and the release report carries the modifiers the keyboard had before the tap.
void CheckTap() {
	KeyboardDevice device;
	HidKeyboard<40> keyboard(device.function);
	device.Configure();

	SIM_EXPECT_EQ(keyboard.Tap(0x04, HidKeyboard<40>::LeftShift), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Modifiers(), 0x00);

	SIM_EXPECT_EQ(keyboard.Press(0x05), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Tap(0x05, 0), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(keyboard.Tap(0x00, 0), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(keyboard.Tap(0xE0, 0), ResultStatus::invalidParameter);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 })
	}, "tap", __LINE__);
}


// TypeText queues two reports per character, uppercase and shifted symbols with Shift. It stops at a character with no key.
void CheckTypeText() {
	KeyboardDevice device;
	HidKeyboard<40> keyboard(device.function);
	device.Configure();

	SIM_EXPECT_EQ(keyboard.TypeText("Hi!"), 3);
	SIM_EXPECT_EQ(keyboard.TypeText(nullptr), 0);
	SIM_EXPECT_EQ(keyboard.TypeText("a\rb"), 1);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x02, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x02, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
	}, "type text", __LINE__);
}


// With room for four reports, TypeText types two characters and returns 2: the third needs two more places.
void CheckTypeTextStopsWhenFull() {
	HidTest::HidDevice<HidTest::KeyboardUsb, HidTest::KeyboardReport, 4> device;
	HidKeyboard<4> keyboard(device.function);
	device.Configure();

	SIM_EXPECT_EQ(keyboard.TypeText("abcd"), 2);
	SIM_EXPECT_EQ(device.function.FreeSlots(), 0);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
	}, "type text while the queue has room for two characters", __LINE__);
}


// A refused report leaves the state as it was: a Tap that finds one place free is busy, and a later press starts from the old state.
void CheckBusyKeepsState() {
	SmallKeyboardDevice device;
	HidKeyboard<2> keyboard(device.function);
	device.Configure();

	SIM_EXPECT_EQ(keyboard.Press(0x04), ResultStatus::ok);
	SIM_EXPECT_EQ(device.function.FreeSlots(), 1);
	SIM_EXPECT_EQ(keyboard.Tap(0x05, 0), ResultStatus::busy);
	SIM_EXPECT_EQ(keyboard.TypeText("b"), 0);
	SIM_EXPECT_EQ(keyboard.Press(0x05), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Press(0x06), ResultStatus::busy);
	SIM_EXPECT_EQ(keyboard.Modifiers(), 0x00);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00 })
	}, "busy keeps state", __LINE__);
}


// The key limit: 32 held keys are kept, the 33rd is refused without a report.
void CheckKeyLimit() {
	KeyboardDevice device;
	HidKeyboard<40> keyboard(device.function);
	device.Configure();

	for (uint8 key = 0x04; key < 0x04 + 32; key++) {
		SIM_EXPECT_EQ(keyboard.Press(key), ResultStatus::ok);
	}

	SIM_EXPECT_EQ(keyboard.Press(0x04 + 32), ResultStatus::outOfMemory);
	std::vector<std::vector<uint8>> sent = Flush(device);
	SIM_EXPECT_EQ(sent.size(), 32);
}


// The LED byte arrives on the OUT endpoint. Only the five LED bits count, and the delegate runs when they change.
void CheckLedsThroughOutEndpoint() {
	KeyboardOutDevice device;
	HidKeyboard<8> keyboard(device.function);
	SIM_EXPECT_EQ(keyboard.Leds(), 0);
	keyboard.SetLedDelegate(HidKeyboard<8>::LedDelegate::Bind<&OnLeds>());
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	std::array<uint8, 1> numLock = { 0x01 };
	SIM_EXPECT(sim.HostOut(0x02, numLock).IsOk());
	SIM_EXPECT_EQ(keyboard.Leds(), 0x01);

	std::array<uint8, 1> same = { 0x01 };
	SIM_EXPECT(sim.HostOut(0x02, same).IsOk());

	// Bits 5 to 7 are padding: 0xE2 is Caps Lock with padding.
	std::array<uint8, 1> padded = { 0xE2 };
	SIM_EXPECT(sim.HostOut(0x02, padded).IsOk());
	SIM_EXPECT_EQ(keyboard.Leds(), 0x02);

	SIM_EXPECT_EQ(ledValues.size(), 2);
	if (ledValues.size() == 2) {
		SIM_EXPECT_EQ(ledValues[0], 0x01);
		SIM_EXPECT_EQ(ledValues[1], 0x02);
	}

	ledValues.clear();
}


void CheckAsciiMapping() {
	USBHidKeyStroke stroke;
	SIM_EXPECT(USBHidAsciiToKey('a', stroke));
	SIM_EXPECT_EQ(stroke.keyCode, 0x04);
	SIM_EXPECT(!stroke.shift);

	SIM_EXPECT(USBHidAsciiToKey('Z', stroke));
	SIM_EXPECT_EQ(stroke.keyCode, 0x1D);
	SIM_EXPECT(stroke.shift);

	SIM_EXPECT(USBHidAsciiToKey('0', stroke));
	SIM_EXPECT_EQ(stroke.keyCode, 0x27);
	SIM_EXPECT(!stroke.shift);

	SIM_EXPECT(USBHidAsciiToKey(')', stroke));
	SIM_EXPECT_EQ(stroke.keyCode, 0x27);
	SIM_EXPECT(stroke.shift);

	SIM_EXPECT(USBHidAsciiToKey('\n', stroke));
	SIM_EXPECT_EQ(stroke.keyCode, 0x28);

	SIM_EXPECT(USBHidAsciiToKey('~', stroke));
	SIM_EXPECT_EQ(stroke.keyCode, 0x35);
	SIM_EXPECT(stroke.shift);

	stroke.keyCode = 0x7E;
	SIM_EXPECT(!USBHidAsciiToKey('\x01', stroke));
	SIM_EXPECT(!USBHidAsciiToKey('\x7F', stroke));
	SIM_EXPECT_EQ(stroke.keyCode, 0x7E);
}


// Movement is split into int8 steps of at most 127 per axis, one report per step, in order.
void CheckMouseSplitting() {
	MouseDevice device;
	HidMouse<8> mouse(device.function);
	device.Configure();

	SIM_EXPECT_EQ(mouse.Move(3, -2), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Move(300, -200), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Scroll(200), ResultStatus::ok);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x00, 0x03, 0xFE, 0x00 }),
		Bytes({ 0x00, 0x7F, 0x81, 0x00 }),
		Bytes({ 0x00, 0x7F, 0xB7, 0x00 }),
		Bytes({ 0x00, 0x2E, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x7F }),
		Bytes({ 0x00, 0x00, 0x00, 0x49 })
	}, "mouse splitting", __LINE__);
}


// Buttons are set with a report of their own, and a click is a press report and a release report. The pending movement goes first.
void CheckMouseButtonsAndClick() {
	MouseDevice device;
	HidMouse<8> mouse(device.function);
	device.Configure();

	SIM_EXPECT_EQ(mouse.SetButtons(HidMouse<8>::ButtonLeft | HidMouse<8>::ButtonRight), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.SetButtons(0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Move(5, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Click(HidMouse<8>::ButtonMiddle), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Buttons(), 0);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x03, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x05, 0x00, 0x00 }),
		Bytes({ 0x04, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00 })
	}, "buttons and click", __LINE__);
}


// With one place left a click is refused before anything is sent, and the buttons stay up.
void CheckMouseClickNeedsTwoPlaces() {
	SmallMouseDevice device;
	HidMouse<2> mouse(device.function);
	device.Configure();

	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Click(HidMouse<2>::ButtonLeft), ResultStatus::busy);
	SIM_EXPECT_EQ(mouse.Buttons(), 0);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, { Bytes({ 0x00, 0x01, 0x00, 0x00 }) }, "click refused with one place", __LINE__);
}


// A full queue makes the movement wait in the mouse: Move returns busy, and Flush sends it once the host has taken the queued reports.
void CheckMouseAccumulatesWhileQueueIsFull() {
	SmallMouseDevice device;
	HidMouse<2> mouse(device.function);
	device.Configure();

	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Move(5, 0), ResultStatus::busy);
	SIM_EXPECT_EQ(mouse.Flush(), ResultStatus::busy);

	std::vector<std::vector<uint8>> first = Flush(device);
	HidTest::ExpectReports(first, { Bytes({ 0x00, 0x01, 0x00, 0x00 }), Bytes({ 0x00, 0x01, 0x00, 0x00 }) }, "queued moves", __LINE__);
	SIM_EXPECT_EQ(mouse.Flush(), ResultStatus::ok);

	std::vector<std::vector<uint8>> second = Flush(device);
	HidTest::ExpectReports(second, { Bytes({ 0x00, 0x05, 0x00, 0x00 }) }, "accumulated move", __LINE__);
}


// SetButtons that the full queue refuses keeps the new buttons, so the next report carries them.
void CheckMouseButtonsKeptWhenBusy() {
	SmallMouseDevice device;
	HidMouse<2> mouse(device.function);
	device.Configure();

	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.SetButtons(HidMouse<2>::ButtonRight), ResultStatus::busy);
	SIM_EXPECT_EQ(mouse.Buttons(), HidMouse<2>::ButtonRight);

	std::vector<std::vector<uint8>> first = Flush(device);
	SIM_EXPECT_EQ(first.size(), 2);
	SIM_EXPECT_EQ(mouse.SetButtons(HidMouse<2>::ButtonRight), ResultStatus::ok);

	std::vector<std::vector<uint8>> second = Flush(device);
	HidTest::ExpectReports(second, { Bytes({ 0x02, 0x00, 0x00, 0x00 }) }, "buttons after busy", __LINE__);
}


// The key codes of the keyboard page are 0x04 (a) to 0xDD. The error codes 0x00 to 0x03, the reserved 0xDE and 0xDF and the modifiers
// above the page are refused, whether pressed or tapped; the modifiers have their own path (Press(0xE0..0xE7) sets a modifier bit).
void CheckKeyCodeRange() {
	KeyboardDevice device;
	HidKeyboard<40> keyboard(device.function);
	device.Configure();

	for (uint8 refused : { 0x00, 0x01, 0x02, 0x03, 0xDE, 0xDF, 0xE8, 0xFF }) {
		SIM_EXPECT_EQ(keyboard.Press(refused), ResultStatus::invalidParameter);
		SIM_EXPECT_EQ(keyboard.Tap(refused, 0), ResultStatus::invalidParameter);
	}

	SIM_EXPECT_EQ(keyboard.Press(0x04), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Press(0xDD), ResultStatus::ok);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x04, 0xDD, 0x00, 0x00, 0x00, 0x00 })
	}, "key codes at the ends of the keyboard page", __LINE__);
}


// F13 (0x68) and F24 (0x73) are keyboard page usages above the boot keyboard's old range of 0x65, so they are sent like any key. The
// reports list the keys in the order they were pressed, and a release removes one key and keeps the order of the others.
void CheckFunctionKeysAreSent() {
	KeyboardDevice device;
	HidKeyboard<40> keyboard(device.function);
	device.Configure();

	SIM_EXPECT_EQ(keyboard.Press(0x68), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Press(0x73), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.Release(0x68), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.ReleaseAll(), ResultStatus::ok);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x68, 0x73, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
	}, "F13 and F24 are sent", __LINE__);
}


// TypeText presses each character with exactly the modifiers its key needs. Ctrl held types the plain key, and Shift held types the
// lower case. The release report restores the modifiers the user holds.
void CheckTypeTextIgnoresHeldModifiers() {
	KeyboardDevice device;
	HidKeyboard<40> keyboard(device.function);
	device.Configure();

	SIM_EXPECT_EQ(keyboard.SetModifiers(HidKeyboard<40>::LeftControl | HidKeyboard<40>::LeftShift), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.TypeText("a"), 1);
	SIM_EXPECT_EQ(keyboard.TypeText("A"), 1);
	SIM_EXPECT_EQ(keyboard.Modifiers(), 0x03);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, {
		Bytes({ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }),
		Bytes({ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
	}, "type text with held modifiers", __LINE__);
}


// A key the user holds is not tapped: the tap would release it. TypeText stops there and returns the count so far.
void CheckTypeTextStopsAtHeldKey() {
	KeyboardDevice device;
	HidKeyboard<40> keyboard(device.function);
	device.Configure();

	SIM_EXPECT_EQ(keyboard.Press(0x04), ResultStatus::ok);
	SIM_EXPECT_EQ(keyboard.TypeText("ab"), 0);
	SIM_EXPECT_EQ(keyboard.Tap(0x04, 0), ResultStatus::invalidParameter);

	std::vector<std::vector<uint8>> sent = Flush(device);
	HidTest::ExpectReports(sent, { Bytes({ 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }) }, "type text stops at a held key", __LINE__);
}


// The pending movement needs a place before the click is sent. With one place free and 46 steps pending, the click needs three places,
// so it is refused with nothing sent, and the movement stays pending.
void CheckMouseClickWaitsForPendingRoom() {
	SmallMouseDevice device;
	HidMouse<2> mouse(device.function);
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	SIM_EXPECT_EQ(mouse.Move(300, 0), ResultStatus::busy);
	device.device.Process();
	std::array<uint8, 64> first{};
	uint32 received = 0;
	SIM_EXPECT_EQ(sim.HostIn(0x81, first, received), ResultStatus::ok);
	SIM_EXPECT_EQ(device.function.FreeSlots(), 1);

	SIM_EXPECT_EQ(mouse.Click(HidMouse<2>::ButtonLeft), ResultStatus::busy);
	SIM_EXPECT_EQ(mouse.Buttons(), 0);
	SIM_EXPECT_EQ(device.function.FreeSlots(), 1);

	std::vector<std::vector<uint8>> sent;
	sent.emplace_back(first.begin(), first.begin() + received);
	std::vector<std::vector<uint8>> rest = HidTest::DrainIn(sim, 0x81);
	sent.insert(sent.end(), rest.begin(), rest.end());
	HidTest::ExpectReports(sent, { Bytes({ 0x00, 0x7F, 0x00, 0x00 }), Bytes({ 0x00, 0x7F, 0x00, 0x00 }) },
		"click waits for the pending movement", __LINE__);

	SIM_EXPECT_EQ(mouse.Flush(), ResultStatus::ok);
	HidTest::ExpectReports(Flush(device), { Bytes({ 0x00, 0x2E, 0x00, 0x00 }) }, "pending movement after the refused click", __LINE__);
}


// The pending movement is bounded to MaxPendingSteps (32767) on each axis. A Move or Scroll that would pass the bound is refused with busy
// and changes nothing, whatever the arguments: the sums are taken in 64 bits, so the largest int32 values cannot overflow. The movement
// that was accepted is all sent, and the refused part is not: 32000 + 1783 steps reach the host, 1784 steps are refused.
void CheckMouseBoundedPending() {
	MouseDevice device;
	HidMouse<8> mouse(device.function);
	device.Configure();

	SIM_EXPECT_EQ(mouse.Move(std::numeric_limits<int32>::max(), 0), ResultStatus::outOfRange);
	SIM_EXPECT_EQ(mouse.Move(std::numeric_limits<int32>::min(), std::numeric_limits<int32>::min()), ResultStatus::outOfRange);
	SIM_EXPECT_EQ(mouse.Scroll(40000), ResultStatus::outOfRange);
	SIM_EXPECT(Flush(device).empty());

	// 8 reports of 127 steps fill the queue and 30984 steps stay pending, so the call is busy: the movement was taken.
	SIM_EXPECT_EQ(mouse.Move(32000, 0), ResultStatus::busy);
	// 30984 + 1784 is one past the bound: refused, with nothing changed.
	SIM_EXPECT_EQ(mouse.Move(1784, 0), ResultStatus::outOfRange);
	// 30984 + 1783 is exactly the bound: accepted, and the queue is still full.
	SIM_EXPECT_EQ(mouse.Move(1783, 0), ResultStatus::busy);

	std::vector<std::vector<uint8>> all;
	for (std::size_t round = 0; round < 300; round++) {
		std::vector<std::vector<uint8>> got = Flush(device);
		all.insert(all.end(), got.begin(), got.end());
		mouse.Flush();
	}

	std::vector<std::vector<uint8>> rest = Flush(device);
	all.insert(all.end(), rest.begin(), rest.end());
	SIM_EXPECT_EQ(mouse.Flush(), ResultStatus::ok);

	int64 sum = 0;
	for (const std::vector<uint8>& report : all) {
		SIM_EXPECT_EQ(report.size(), 4);
		sum += static_cast<int8>(report[1]);
	}

	SIM_EXPECT_EQ(sum, 33783);
}


// The movement the mouse holds when the host deconfigures the device is dropped with the queue, and so is the wheel. After the next
// enumeration the host sees only the movement made from then on, not one jump for the movement made while it was away.
void CheckPendingDroppedOnDeconfigure() {
	SmallMouseDevice device;
	HidMouse<2> mouse(device.function);
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Move(5, 0), ResultStatus::busy);
	SIM_EXPECT_EQ(mouse.Scroll(3), ResultStatus::busy);

	// SET_CONFIGURATION 0 deconfigures the function.
	SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 0, 0, 0)), ResultStatus::ok);
	device.Enumerate();
	SIM_EXPECT(device.device.GetState() == USBDeviceState::Configured);

	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	HidTest::ExpectReports(Flush(device), { Bytes({ 0x00, 0x01, 0x00, 0x00 }) }, "movement after deconfigure", __LINE__);
	SIM_EXPECT_EQ(mouse.Flush(), ResultStatus::ok);
}


// A bus reset drops the pending movement the same way. The reset keeps the function configured until the host enumerates again.
void CheckPendingDroppedOnBusReset() {
	SmallMouseDevice device;
	HidMouse<2> mouse(device.function);
	device.Configure();

	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Move(5, 0), ResultStatus::busy);

	device.Enumerate();
	SIM_EXPECT(device.device.GetState() == USBDeviceState::Configured);

	SIM_EXPECT_EQ(mouse.Move(1, 0), ResultStatus::ok);
	HidTest::ExpectReports(Flush(device), { Bytes({ 0x00, 0x01, 0x00, 0x00 }) }, "movement after bus reset", __LINE__);
}


// A relative device's idle repeat and GET_REPORT carry the buttons and no motion, so a repeat does not move the pointer again.
void CheckMouseIdleIsStill() {
	MouseDevice device;
	HidMouse<8> mouse(device.function);
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	SIM_EXPECT_EQ(mouse.SetButtons(HidMouse<8>::ButtonLeft), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.Move(5, -3), ResultStatus::ok);
	HidTest::ExpectReports(Flush(device), { Bytes({ 0x01, 0x00, 0x00, 0x00 }), Bytes({ 0x01, 0x05, 0xFD, 0x00 }) }, "mouse frames", __LINE__);

	SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x21, 0x0A, static_cast<uint16>(1 << 8), 0, 0)), ResultStatus::ok);
	std::this_thread::sleep_for(std::chrono::milliseconds(12));
	device.device.Process();
	HidTest::ExpectReports(HidTest::DrainIn(sim, 0x81), { Bytes({ 0x01, 0x00, 0x00, 0x00 }) }, "idle repeat has no motion", __LINE__);

	std::array<uint8, 4> answer{};
	uint32 received = 0;
	ResultStatus status = SimHost::Control(sim, SimHost::MakeSetup(0xA1, 0x01, 0x0100, 0, 4), std::span<const uint8>(), answer, received);
	SIM_EXPECT_EQ(status, ResultStatus::ok);
	std::vector<uint8> got(answer.begin(), answer.begin() + received);
	SIM_EXPECT_BYTES(got, Bytes({ 0x01, 0x00, 0x00, 0x00 }));
}


}


int main() {
	CheckPressReleaseModifiers();
	CheckRollover();
	CheckTap();
	CheckTypeText();
	CheckTypeTextStopsWhenFull();
	CheckBusyKeepsState();
	CheckKeyLimit();
	CheckLedsThroughOutEndpoint();
	CheckAsciiMapping();
	CheckMouseSplitting();
	CheckMouseButtonsAndClick();
	CheckMouseClickNeedsTwoPlaces();
	CheckMouseAccumulatesWhileQueueIsFull();
	CheckMouseButtonsKeptWhenBusy();
	CheckKeyCodeRange();
	CheckFunctionKeysAreSent();
	CheckTypeTextIgnoresHeldModifiers();
	CheckTypeTextStopsAtHeldKey();
	CheckMouseClickWaitsForPendingRoom();
	CheckMouseBoundedPending();
	CheckPendingDroppedOnDeconfigure();
	CheckPendingDroppedOnBusReset();
	CheckMouseIdleIsStill();
	return SimTest::Finish("TestHidInput");
}
