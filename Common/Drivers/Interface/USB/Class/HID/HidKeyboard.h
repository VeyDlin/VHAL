#pragma once
#include <Drivers/Interface/USB/Class/HID/HidFunction.h>
#include <Utilities/Callback/Delegate.h>
#include <array>
#include <cstddef>
#include <span>


// A key of the US layout: its key code (HID Usage Tables, Keyboard page) and whether Shift has to be held for it.
struct USBHidKeyStroke {
	uint8 keyCode = 0;
	bool shift = false;
};


// Characters that are not a letter or a digit, with their key code and Shift state. Letters and digits are computed.
struct USBHidAsciiEntry {
	char character;
	uint8 keyCode;
	bool shift;
};


inline constexpr USBHidAsciiEntry USBHidAsciiTable[] = {
	{ '\n', 0x28, false },
	{ '\t', 0x2B, false },
	{ ' ', 0x2C, false },
	{ '-', 0x2D, false },
	{ '=', 0x2E, false },
	{ '[', 0x2F, false },
	{ ']', 0x30, false },
	{ '\\', 0x31, false },
	{ ';', 0x33, false },
	{ '\'', 0x34, false },
	{ '`', 0x35, false },
	{ ',', 0x36, false },
	{ '.', 0x37, false },
	{ '/', 0x38, false },
	{ '!', 0x1E, true },
	{ '@', 0x1F, true },
	{ '#', 0x20, true },
	{ '$', 0x21, true },
	{ '%', 0x22, true },
	{ '^', 0x23, true },
	{ '&', 0x24, true },
	{ '*', 0x25, true },
	{ '(', 0x26, true },
	{ ')', 0x27, true },
	{ '_', 0x2D, true },
	{ '+', 0x2E, true },
	{ '{', 0x2F, true },
	{ '}', 0x30, true },
	{ '|', 0x31, true },
	{ ':', 0x33, true },
	{ '"', 0x34, true },
	{ '~', 0x35, true },
	{ '<', 0x36, true },
	{ '>', 0x37, true },
	{ '?', 0x38, true },
};


// The key that types a character on the US layout. Returns false for a character with no key here (any byte outside printable ASCII
// and Tab and Enter), and then stroke is not changed.
constexpr bool USBHidAsciiToKey(char character, USBHidKeyStroke& stroke) {
	if (character >= 'a' && character <= 'z') {
		stroke.keyCode = static_cast<uint8>(0x04 + (character - 'a'));
		stroke.shift = false;
		return true;
	}

	if (character >= 'A' && character <= 'Z') {
		stroke.keyCode = static_cast<uint8>(0x04 + (character - 'A'));
		stroke.shift = true;
		return true;
	}

	if (character >= '1' && character <= '9') {
		stroke.keyCode = static_cast<uint8>(0x1E + (character - '1'));
		stroke.shift = false;
		return true;
	}

	if (character == '0') {
		stroke.keyCode = 0x27;
		stroke.shift = false;
		return true;
	}

	for (const USBHidAsciiEntry& entry : USBHidAsciiTable) {
		if (entry.character == character) {
			stroke.keyCode = entry.keyCode;
			stroke.shift = entry.shift;
			return true;
		}
	}

	return false;
}


// Keyboard on a boot keyboard interface (USBHidReportBuilder::BootKeyboard()). It keeps the modifiers and the held keys and sends a
// full report with each change: the report is the state after the change, so a report that waits in the queue is never out of date
// at its send. Key codes of the modifier range (0xE0 to 0xE7) change the modifier byte. Up to six keys are reported; a seventh held
// key makes the report ErrorRollOver (every key slot 0x01) until a key is released.
//
// Each call returns the status of its report. When the queue is full the call returns busy and leaves the state as it was, so the
// caller can retry. Tap() needs two free places, one for the press and one for the release, and TypeText() types as many characters
// as the queue has room for.
//
// The LED byte of the host's output report is kept, and LedDelegate runs in interrupt context when it changes. The keyboard registers
// itself as the function's output delegate, so output reports reach LedDelegate and not a delegate of the application. The constructor
// cannot report a refusal: the registration is refused once the function is configured, and then LEDs are never delivered. Create the
// keyboard with the function and before the device starts, and keep it alive for as long as the function is, since the function holds
// a delegate to it.
template<std::size_t QueueDepth>
class HidKeyboard {
public:
	// Called in interrupt context with the five LED bits: bit 0 Num Lock, bit 1 Caps Lock, bit 2 Scroll Lock, bit 3 Compose, bit 4 Kana.
	using LedDelegate = Callback::Delegate<void(uint8 leds)>;


	static constexpr uint8 LeftControl = 0x01;
	static constexpr uint8 LeftShift = 0x02;
	static constexpr uint8 LeftAlt = 0x04;
	static constexpr uint8 LeftGui = 0x08;
	static constexpr uint8 ErrorRollOver = 0x01;
	static constexpr uint8 ModifierFirstKey = 0xE0;
	static constexpr uint8 ModifierLastKey = 0xE7;
	static constexpr std::size_t MaxHeldKeys = 32;
	static constexpr std::size_t KeySlots = 6;


private:
	static constexpr std::size_t BootReportLength = 8;
	static constexpr uint8 LedMask = 0x1F;
	// The keyboard page's key usages: 0x04 (a) up to 0xDD. 0x01 to 0x03 are the error codes of the report and 0xDE and 0xDF are
	// reserved, so the range is not the whole byte; the modifiers are the usages above it, and they go through the modifier byte.
	static constexpr uint8 FirstKeyCode = 0x04;
	static constexpr uint8 LastKeyCode = 0xDD;


	// The state a change starts from, so a change that the queue refuses can be undone.
	struct KeyState {
		uint8 modifiers = 0;
		std::array<uint8, MaxHeldKeys> held{};
		std::size_t heldCount = 0;
	};


	HidFunction<QueueDepth>& hid;
	uint8 modifiers = 0;
	std::array<uint8, MaxHeldKeys> held{};
	std::size_t heldCount = 0;
	uint8 leds = 0;
	LedDelegate ledDelegate;


public:
	explicit HidKeyboard(HidFunction<QueueDepth>& function)
		: hid(function) {
		hid.SetOutputDelegate(HidFunction<QueueDepth>::OutputDelegate::template Bind<HidKeyboard, &HidKeyboard::OnOutputReport>(this));
	}


	// Presses a key or a modifier and sends the report. Pressing a key that is held is a no-op.
	ResultStatus Press(uint8 keyCode) {
		if (IsModifierKey(keyCode)) {
			uint8 bit = ModifierBit(keyCode);
			if ((modifiers & bit) != 0) {
				return ResultStatus::ok;
			}

			KeyState previous = Save();
			modifiers = static_cast<uint8>(modifiers | bit);
			return Commit(previous);
		}

		if (!IsKeyCode(keyCode)) {
			return ResultStatus::invalidParameter;
		}

		if (IsHeld(keyCode)) {
			return ResultStatus::ok;
		}

		if (heldCount == MaxHeldKeys) {
			return ResultStatus::outOfMemory;
		}

		KeyState previous = Save();
		AddHeld(keyCode);
		return Commit(previous);
	}


	// Releases a key or a modifier and sends the report. Releasing a key that is not held is a no-op.
	ResultStatus Release(uint8 keyCode) {
		if (IsModifierKey(keyCode)) {
			uint8 bit = ModifierBit(keyCode);
			if ((modifiers & bit) == 0) {
				return ResultStatus::ok;
			}

			KeyState previous = Save();
			modifiers = static_cast<uint8>(modifiers & ~bit);
			return Commit(previous);
		}

		if (!IsHeld(keyCode)) {
			return ResultStatus::ok;
		}

		KeyState previous = Save();
		RemoveHeld(keyCode);
		return Commit(previous);
	}


	// Releases every key and every modifier with one report.
	ResultStatus ReleaseAll() {
		if (modifiers == 0 && heldCount == 0) {
			return ResultStatus::ok;
		}

		KeyState previous = Save();
		modifiers = 0;
		heldCount = 0;
		return Commit(previous);
	}


	// Sets the modifier byte (LeftControl, LeftShift, ... as the bits of the report) and sends the report.
	ResultStatus SetModifiers(uint8 mask) {
		if (mask == modifiers) {
			return ResultStatus::ok;
		}

		KeyState previous = Save();
		modifiers = mask;
		return Commit(previous);
	}


	// Presses the key with the modifiers and releases it again: two reports, the press and the release. The press carries the modifiers
	// the keyboard holds plus tapModifiers; the release report carries the modifiers the keyboard had before. A key that is held already
	// is refused.
	ResultStatus Tap(uint8 keyCode, uint8 tapModifiers) {
		return TapWith(keyCode, static_cast<uint8>(modifiers | tapModifiers));
	}


	// Types the text on the US layout, one tap per character. A character is pressed with exactly the modifiers its key needs: Shift for
	// the upper case and the shifted symbols, none otherwise. The modifiers the user holds are not added, so Ctrl held types the plain
	// key. The release restores them. Typing stops at the first character with no key (such as a non-ASCII byte or a carriage return),
	// at a character whose key is held (a tap would release it), and at the first character that does not fit the queue. Returns the
	// number of characters typed.
	std::size_t TypeText(const char* text) {
		std::size_t typed = 0;
		if (text == nullptr) {
			return 0;
		}

		for (const char* cursor = text; *cursor != '\0'; cursor++) {
			USBHidKeyStroke stroke;
			if (!USBHidAsciiToKey(*cursor, stroke)) {
				break;
			}

			uint8 pressModifiers = stroke.shift ? LeftShift : static_cast<uint8>(0);
			if (TapWith(stroke.keyCode, pressModifiers) != ResultStatus::ok) {
				break;
			}

			typed++;
		}

		return typed;
	}


	uint8 Modifiers() const {
		return modifiers;
	}


	// The LED bits the host last sent.
	uint8 Leds() const {
		return leds;
	}


	// Set before the device starts.
	void SetLedDelegate(LedDelegate value) {
		ledDelegate = value;
	}


private:
	static bool IsModifierKey(uint8 keyCode) {
		return keyCode >= ModifierFirstKey && keyCode <= ModifierLastKey;
	}


	// A key of the report's key array: a usage of the keyboard page. The error codes below 0x04 and the modifiers above the page are refused.
	static bool IsKeyCode(uint8 keyCode) {
		return keyCode >= FirstKeyCode && keyCode <= LastKeyCode;
	}


	// The press report carries pressModifiers, and the release report carries the modifiers the keyboard had before the tap.
	ResultStatus TapWith(uint8 keyCode, uint8 pressModifiers) {
		if (!IsKeyCode(keyCode) || IsHeld(keyCode)) {
			return ResultStatus::invalidParameter;
		}

		if (heldCount == MaxHeldKeys) {
			return ResultStatus::outOfMemory;
		}

		if (hid.FreeSlots() < 2) {
			return ResultStatus::busy;
		}

		KeyState previous = Save();
		modifiers = pressModifiers;
		AddHeld(keyCode);
		ResultStatus status = Send();
		if (status != ResultStatus::ok) {
			Restore(previous);
			return status;
		}

		modifiers = previous.modifiers;
		RemoveHeld(keyCode);
		return Send();
	}


	static uint8 ModifierBit(uint8 keyCode) {
		return static_cast<uint8>(1u << (keyCode - ModifierFirstKey));
	}


	bool IsHeld(uint8 keyCode) const {
		for (std::size_t index = 0; index < heldCount; index++) {
			if (held[index] == keyCode) {
				return true;
			}
		}

		return false;
	}


	// The caller checked that there is room.
	void AddHeld(uint8 keyCode) {
		held[heldCount] = keyCode;
		heldCount++;
	}


	// Removes the key and keeps the order of the others, so the report's key slots keep the order of the presses.
	void RemoveHeld(uint8 keyCode) {
		for (std::size_t index = 0; index < heldCount; index++) {
			if (held[index] == keyCode) {
				for (std::size_t move = index; move + 1 < heldCount; move++) {
					held[move] = held[move + 1];
				}

				heldCount--;
				return;
			}
		}
	}


	KeyState Save() const {
		KeyState state;
		state.modifiers = modifiers;
		state.held = held;
		state.heldCount = heldCount;
		return state;
	}


	void Restore(const KeyState& state) {
		modifiers = state.modifiers;
		held = state.held;
		heldCount = state.heldCount;
	}


	// Sends the current state. A refused report restores the state the change started from.
	ResultStatus Commit(const KeyState& previous) {
		ResultStatus status = Send();
		if (status != ResultStatus::ok) {
			Restore(previous);
		}

		return status;
	}


	ResultStatus Send() {
		std::array<uint8, BootReportLength> report{};
		report[0] = modifiers;
		if (heldCount > KeySlots) {
			for (std::size_t index = 0; index < KeySlots; index++) {
				report[2 + index] = ErrorRollOver;
			}
		} else {
			for (std::size_t index = 0; index < heldCount; index++) {
				report[2 + index] = held[index];
			}
		}

		return hid.SendReport(report);
	}


	void OnOutputReport(std::span<const uint8> report) {
		if (report.empty()) {
			return;
		}

		uint8 value = static_cast<uint8>(report[0] & LedMask);
		if (value == leds) {
			return;
		}

		leds = value;
		if (ledDelegate) {
			ledDelegate(value);
		}
	}
};
